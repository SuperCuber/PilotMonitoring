#include "voice_service.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include "grammar-parser.h"
#include "whisper.h"

namespace {

using Microsoft::WRL::ComPtr;

bool is_float_format(const WAVEFORMATEX& format) {
    if (format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        return true;
    }
    if (format.wFormatTag != WAVE_FORMAT_EXTENSIBLE || format.cbSize < 22) {
        return false;
    }
    const auto& extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE&>(format);
    return extensible.SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
}

bool is_pcm_format(const WAVEFORMATEX& format) {
    if (format.wFormatTag == WAVE_FORMAT_PCM) {
        return true;
    }
    if (format.wFormatTag != WAVE_FORMAT_EXTENSIBLE || format.cbSize < 22) {
        return false;
    }
    const auto& extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE&>(format);
    return extensible.SubFormat == KSDATAFORMAT_SUBTYPE_PCM;
}

float sample_as_float(const BYTE* source, int bits_per_sample, bool floating_point) {
    if (floating_point && bits_per_sample == 32) {
        float sample = 0.0F;
        std::memcpy(&sample, source, sizeof(sample));
        return std::clamp(sample, -1.0F, 1.0F);
    }

    if (bits_per_sample == 16) {
        std::int16_t sample = 0;
        std::memcpy(&sample, source, sizeof(sample));
        return static_cast<float>(sample) / 32768.0F;
    }
    if (bits_per_sample == 32) {
        std::int32_t sample = 0;
        std::memcpy(&sample, source, sizeof(sample));
        return static_cast<float>(sample / 2147483648.0);
    }
    return 0.0F;
}

std::vector<float> resample_to_whisper_rate(
    const std::vector<float>& input,
    unsigned int source_rate) {
    if (input.empty() || source_rate == WHISPER_SAMPLE_RATE) {
        return input;
    }

    const auto output_count = static_cast<std::size_t>(
        std::llround(static_cast<double>(input.size()) * WHISPER_SAMPLE_RATE / source_rate));
    std::vector<float> output(output_count);
    const double step = static_cast<double>(source_rate) / WHISPER_SAMPLE_RATE;

    for (std::size_t output_index = 0; output_index < output.size(); ++output_index) {
        const double source_position = output_index * step;
        const auto left = static_cast<std::size_t>(source_position);
        const auto right = (std::min)(left + 1, input.size() - 1);
        const float fraction = static_cast<float>(source_position - left);
        output[output_index] = input[left] + (input[right] - input[left]) * fraction;
    }
    return output;
}

} // namespace

VoiceService::VoiceService(std::string model_path, grammar_parser::parse_state grammar)
    : model_path_(std::move(model_path)), grammar_(std::move(grammar)) {
}

VoiceService::~VoiceService() {
    stop();
}

void VoiceService::start() {
    if (running_.exchange(true)) {
        return;
    }
    worker_ = std::thread(&VoiceService::worker_main, this);
}

void VoiceService::set_listening(bool active) {
    const bool was_listening = listening_.exchange(active);
    if (active && !was_listening) {
        std::lock_guard lock(samples_mutex_);
        samples_.clear();
    }
    wake_cv_.notify_all();
    samples_cv_.notify_all();
}

void VoiceService::set_grammar(grammar_parser::parse_state grammar) {
    std::lock_guard lock(grammar_mutex_);
    grammar_ = std::move(grammar);
}

void VoiceService::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    listening_.store(false);
    wake_cv_.notify_all();
    samples_cv_.notify_all();
    if (capture_thread_.joinable()) {
        capture_thread_.join();
    }
    if (worker_.joinable()) {
        worker_.join();
    }
}

std::optional<VoiceService::Result> VoiceService::pop_result() {
    std::lock_guard lock(results_mutex_);
    if (results_.empty()) {
        return std::nullopt;
    }
    Result result = std::move(results_.front());
    results_.erase(results_.begin());
    return result;
}

void VoiceService::push_result(bool recognized, std::string text) {
    std::lock_guard lock(results_mutex_);
    results_.push_back({recognized, std::move(text)});
}

void VoiceService::worker_main() {
    const HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com_result)) {
        push_result(false, "could not initialize the Windows audio subsystem");
        return;
    }

    const auto context_params = whisper_context_default_params();
    context_ = whisper_init_from_file_with_params(model_path_.c_str(), context_params);
    if (context_ == nullptr) {
        push_result(false, "could not load Whisper model: " + model_path_);
        CoUninitialize();
        return;
    }

    push_result(false, "Whisper model ready; hold the PilotMonitoring PTT binding to speak.");

    while (running_.load()) {
        std::unique_lock lock(wake_mutex_);
        wake_cv_.wait(lock, [this] { return !running_.load() || listening_.load(); });
        lock.unlock();
        if (!running_.load()) {
            break;
        }

        if (capture_thread_.joinable()) {
            capture_thread_.join();
        }
        capture_thread_ = std::thread(&VoiceService::capture_main, this);

        // Whisper works on complete buffers. Re-running it on snapshots lets
        // recognition overlap capture, while the result remains private until
        // the PTT command ends.
        std::size_t last_sample_count = 0;
        while (running_.load() && listening_.load()) {
            std::unique_lock samples_lock(samples_mutex_);
            samples_cv_.wait_for(samples_lock, std::chrono::milliseconds(250));
            const auto sample_count = samples_.size();
            samples_lock.unlock();

            if (sample_count < 8000 || sample_count == last_sample_count) {
                continue;
            }
            auto samples = snapshot_samples();
            if (!samples.empty()) {
                transcribe(samples, false);
            }
            last_sample_count = sample_count;
        }

        if (capture_thread_.joinable()) {
            capture_thread_.join();
        }
        if (!running_.load()) {
            break;
        }
        const auto samples = snapshot_samples();
        if (!samples.empty()) {
            if (auto transcript = transcribe(samples, true)) {
                push_result(true, std::move(*transcript));
            }
        }
    }

    whisper_free(context_);
    context_ = nullptr;
    CoUninitialize();
}

void VoiceService::capture_main() {
    const HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com_result)) {
        push_result(false, "could not initialize the Windows audio subsystem");
    } else {
        capture_phrase();
        CoUninitialize();
    }
    samples_cv_.notify_all();
}

void VoiceService::capture_phrase() {
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice> device;
    ComPtr<IAudioClient> audio_client;
    ComPtr<IAudioCaptureClient> capture_client;

    HRESULT result = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (FAILED(result) || FAILED(enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device)) ||
        FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                reinterpret_cast<void**>(audio_client.GetAddressOf())))) {
        push_result(false, "could not open the default microphone");
        return;
    }

    WAVEFORMATEX* format = nullptr;
    if (FAILED(audio_client->GetMixFormat(&format))) {
        push_result(false, "could not read the microphone format");
        return;
    }

    const unsigned int sample_rate = format->nSamplesPerSec;
    const unsigned int channels = format->nChannels;
    const unsigned int bytes_per_sample = format->wBitsPerSample / 8;
    const bool floating_point = is_float_format(*format);
    const bool pcm = is_pcm_format(*format);

    if ((!floating_point && !pcm) || bytes_per_sample == 0 || channels == 0) {
        CoTaskMemFree(format);
        push_result(false, "the default microphone uses an unsupported audio format");
        return;
    }

    result = audio_client->Initialize(
        AUDCLNT_SHAREMODE_SHARED, 0, 0, 0, format, nullptr);
    CoTaskMemFree(format);
    if (FAILED(result) || FAILED(audio_client->GetService(IID_PPV_ARGS(&capture_client))) ||
        FAILED(audio_client->Start())) {
        push_result(false, "could not start microphone capture");
        return;
    }

    std::vector<float> mono_samples;
    while (running_.load() && listening_.load()) {
        UINT32 packet_size = 0;
        while (SUCCEEDED(capture_client->GetNextPacketSize(&packet_size)) && packet_size != 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (FAILED(capture_client->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) {
                break;
            }

            if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0) {
                mono_samples.insert(mono_samples.end(), frames, 0.0F);
            } else {
                for (UINT32 frame = 0; frame < frames; ++frame) {
                    float mixed_sample = 0.0F;
                    for (unsigned int channel = 0; channel < channels; ++channel) {
                        const auto offset = (static_cast<std::size_t>(frame) * channels + channel) * bytes_per_sample;
                        mixed_sample += sample_as_float(data + offset, bytes_per_sample * 8, floating_point);
                    }
                    mono_samples.push_back(mixed_sample / channels);
                }
            }
            capture_client->ReleaseBuffer(frames);

            {
                std::lock_guard lock(samples_mutex_);
                samples_ = std::move(resample_to_whisper_rate(mono_samples, sample_rate));
            }
            samples_cv_.notify_all();
        }
        Sleep(10);
    }

    audio_client->Stop();
}

std::vector<float> VoiceService::snapshot_samples() {
    std::lock_guard lock(samples_mutex_);
    return samples_;
}

std::optional<std::string> VoiceService::transcribe(
    const std::vector<float>& samples, bool report_errors) {
    grammar_parser::parse_state grammar;
    {
        std::lock_guard lock(grammar_mutex_);
        grammar = grammar_;
    }
    const bool use_grammar =
         !grammar.rules.empty() && grammar.symbol_ids.find("root") != grammar.symbol_ids.end();
    auto params = whisper_full_default_params(
        use_grammar ? WHISPER_SAMPLING_BEAM_SEARCH : WHISPER_SAMPLING_GREEDY);
    params.print_progress = false;
    params.print_realtime = false;
    params.print_timestamps = false;
    params.translate = false;
    params.language = "en";
    params.no_context = true;
    params.single_segment = true;
    params.beam_search.beam_size = 5;

    std::vector<const whisper_grammar_element*> grammar_rules;
    if (use_grammar) {
        grammar_rules = grammar.c_rules();
        params.grammar_rules = grammar_rules.data();
        params.n_grammar_rules = grammar_rules.size();
        params.i_start_rule = grammar.symbol_ids.at("root");
        params.grammar_penalty = 100.0F;
    }

    if (whisper_full(context_, params, samples.data(), static_cast<int>(samples.size())) != 0) {
        if (report_errors) push_result(false, "Whisper could not transcribe the captured audio");
        return std::nullopt;
    }

    std::string transcript;
    const int segment_count = whisper_full_n_segments(context_);
    for (int segment = 0; segment < segment_count; ++segment) {
        transcript += whisper_full_get_segment_text(context_, segment);
    }
    if (transcript.empty()) {
        if (report_errors) push_result(false, "no speech was recognized");
        return std::nullopt;
    }
    if (transcript.front() == ' ') transcript.erase(0, 1); // Grammar commonly emits a leading space.
    return transcript;
}
