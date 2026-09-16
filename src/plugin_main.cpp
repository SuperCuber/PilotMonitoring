#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "XPLMDataAccess.h"
#include "XPLMMenus.h"
#include "XPLMPlugin.h"
#include "XPLMProcessing.h"
#include "XPLMSound.h"
#include "XPLMUtilities.h"
#include "whisper.h"

#include "execution_engine.h"
#include "voice_service.h"

namespace {

constexpr char kPluginName[] = "Pilot Monitoring";
constexpr char kPluginSignature[] = "com.example.pilotmonitoring";
constexpr char kPluginDescription[] = "Voice-command plugin scaffold powered by whisper.cpp.";
constexpr char kPttCommandName[] = "pilotmonitoring/ptt";
char kMenuActionReloadPlugins[] = "reload_plugins";

XPLMCommandRef g_ptt_command = nullptr;
XPLMMenuID g_menu = nullptr;
std::unique_ptr<VoiceService> g_voice_service;
std::unique_ptr<ExecutionEngine> g_execution_engine;
bool g_flight_loop_registered = false;
XPLMFlightLoopID g_aircraft_flight_loop = nullptr;
std::string g_last_aircraft_load_error;

void log_message(const char* message) {
    XPLMDebugString(message);
}

void log_line(std::string_view line) {
    std::string text(line);
    text.push_back('\n');
    log_message(text.c_str());
}

int handle_ptt_command(XPLMCommandRef, XPLMCommandPhase phase, void*) {
    if (!g_voice_service) {
        return 1;
    }

    if (phase == xplm_CommandBegin) {
        log_message("Pilot Monitoring: PTT pressed; listening.\n");
        g_voice_service->set_listening(true);
    } else if (phase == xplm_CommandEnd) {
        log_message("Pilot Monitoring: PTT released; finalizing command.\n");
        g_voice_service->set_listening(false);
    }
    return 1;
}

std::filesystem::path plugin_resources_path() {
    char plugin_path[512]{};
    XPLMGetPluginInfo(XPLMGetMyID(), nullptr, plugin_path, nullptr, nullptr);
    return std::filesystem::path(plugin_path).parent_path().parent_path() / "resources";
}

std::filesystem::path model_path() {
    return plugin_resources_path() / "models" / "ggml-base.en.bin";
}

std::filesystem::path aircraft_commands_path(std::string& error) {
    constexpr std::string_view kAircraftDataref = "sim/aircraft/view/acf_ICAO";
    const XPLMDataRef ref = XPLMFindDataRef(std::string(kAircraftDataref).c_str());
    if (ref == nullptr) {
        error = "aircraft ICAO dataref unavailable: " + std::string(kAircraftDataref);
        return {};
    }

    const int size = XPLMGetDatab(ref, nullptr, 0, 0);
    if (size <= 0) {
        error = "aircraft ICAO dataref is empty";
        return {};
    }

    std::string icao(static_cast<std::size_t>(size), '\0');
    const int bytes_read = XPLMGetDatab(ref, icao.data(), 0, size);
    if (bytes_read <= 0) {
        error = "could not read aircraft ICAO dataref";
        return {};
    }
    icao.resize(static_cast<std::size_t>(bytes_read));
    while (!icao.empty() &&
           (icao.back() == '\0' || std::isspace(static_cast<unsigned char>(icao.back())))) {
        icao.pop_back();
    }
    if (icao.empty()) {
        error = "aircraft ICAO dataref is empty";
        return {};
    }
    for (char& character : icao) {
        character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
        if (!std::isalnum(static_cast<unsigned char>(character))) {
            error = "invalid aircraft ICAO: " + icao;
            return {};
        }
    }
    return plugin_resources_path() / (icao + ".lua");
}

struct PcmSound {
    std::vector<std::uint8_t> samples;
    int sample_rate = 0;
    int channels = 0;
};

std::uint16_t read_u16(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(data[0]) |
           (static_cast<std::uint16_t>(data[1]) << 8);
}

std::uint32_t read_u32(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8) |
           (static_cast<std::uint32_t>(data[2]) << 16) |
           (static_cast<std::uint32_t>(data[3]) << 24);
}

std::optional<PcmSound> load_pcm_wav(const std::filesystem::path& path, std::string& error) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) { error = "could not open sound file: " + path.string(); return std::nullopt; }
    const auto file_size = file.tellg();
    if (file_size < 12) { error = "sound file is too small: " + path.string(); return std::nullopt; }
    file.seekg(0);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(file_size));
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file) { error = "could not read sound file: " + path.string(); return std::nullopt; }
    if (std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
        error = "sound file is not a RIFF/WAVE file: " + path.string(); return std::nullopt;
    }

    std::size_t offset = 12;
    int format = 0;
    int channels = 0;
    int sample_rate = 0;
    std::vector<std::uint8_t> samples;
    while (offset + 8 <= bytes.size()) {
        const auto* chunk = bytes.data() + offset;
        const std::size_t chunk_size = read_u32(chunk + 4);
        offset += 8;
        if (chunk_size > bytes.size() - offset) {
            error = "sound file contains an invalid chunk: " + path.string(); return std::nullopt;
        }
        if (std::memcmp(chunk, "fmt ", 4) == 0 && chunk_size >= 16) {
            format = read_u16(bytes.data() + offset);
            channels = read_u16(bytes.data() + offset + 2);
            sample_rate = static_cast<int>(read_u32(bytes.data() + offset + 4));
            const int bits_per_sample = read_u16(bytes.data() + offset + 14);
            if (format != 1 || bits_per_sample != 16 || channels <= 0 || sample_rate <= 0) {
                error = "sound must be uncompressed 16-bit PCM: " + path.string(); return std::nullopt;
            }
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            samples.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                           bytes.begin() + static_cast<std::ptrdiff_t>(offset + chunk_size));
        }
        offset += chunk_size + (chunk_size & 1U);
    }
    if (format != 1 || channels <= 0 || sample_rate <= 0 || samples.empty()) {
        error = "sound file is missing valid PCM audio data: " + path.string(); return std::nullopt;
    }
    return PcmSound{std::move(samples), sample_rate, channels};
}

void release_pcm_sound(void* refcon, FMOD_RESULT) {
    delete static_cast<PcmSound*>(refcon);
}

bool valid_sound_filename(std::string_view filename) {
    if (filename.empty()) return false;
    for (const unsigned char character : filename) {
        if (!std::isalnum(character) && character != '_') return false;
    }
    return true;
}

void play_sound(const PlaySoundAction& action) {
    if (!valid_sound_filename(action.filename)) {
        log_line("Pilot Monitoring: invalid sound filename: " + action.filename);
        return;
    }
    std::string error;
    auto sound = load_pcm_wav(plugin_resources_path() / "audio" / (action.filename + ".wav"), error);
    if (!sound) { log_line("Pilot Monitoring: " + error); return; }
    auto* sound_data = new PcmSound(std::move(*sound));
    XPLMPlayPCMOnBus(sound_data->samples.data(), static_cast<std::uint32_t>(sound_data->samples.size()),
                     FMOD_SOUND_FORMAT_PCM16, sound_data->sample_rate, sound_data->channels, 0,
                     xplm_AudioUI, release_pcm_sound, sound_data);
}

void log_grammar(std::string_view grammar_text) {
    log_message("Pilot Monitoring: generated grammar:\n");

    std::istringstream stream{std::string(grammar_text)};
    std::string line;
    while (std::getline(stream, line)) {
        log_line(line);
    }
}

class XPlaneDatarefHost final : public DatarefHost {
public:
    std::optional<int> get_integer(std::string_view name, std::string& error) override {
        const XPLMDataRef ref = XPLMFindDataRef(std::string(name).c_str());
        if (!ref) { error = "dataref unavailable: " + std::string(name); return std::nullopt; }
        return XPLMGetDatai(ref);
    }
    std::optional<float> get_float(std::string_view name, std::string& error) override {
        const XPLMDataRef ref = XPLMFindDataRef(std::string(name).c_str());
        if (!ref) { error = "dataref unavailable: " + std::string(name); return std::nullopt; }
        return XPLMGetDataf(ref);
    }
    std::optional<bool> get_boolean(std::string_view name, std::string& error) override {
        const auto value = get_integer(name, error);
        if (!value) return std::nullopt;
        return *value != 0;
    }
};

XPlaneDatarefHost g_dataref_host;

void execute_action(const Action& action) {
    std::visit([](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, TriggerCommandAction>) {
            const XPLMCommandRef command_ref = XPLMFindCommand(value.command.c_str());
            if (command_ref == nullptr) {
                log_message("Pilot Monitoring: command_once target is unavailable.\n");
                return;
            }
            XPLMCommandOnce(command_ref);
            return;
        } else if constexpr (std::is_same_v<T, SpeakAction>) {
            XPLMSpeakString(value.message.c_str());
            return;
        } else if constexpr (std::is_same_v<T, PlaySoundAction>) {
            play_sound(value);
            return;
        } else {
        const std::string& dataref_name = value.dataref;
        const XPLMDataRef dataref = XPLMFindDataRef(dataref_name.c_str());
        if (dataref == nullptr) {
            if constexpr (std::is_same_v<T, SetIntegerDatarefAction>) {
                log_line("Pilot Monitoring: set_dataref_integer target is unavailable: " + dataref_name);
            } else if constexpr (std::is_same_v<T, SetFloatDatarefAction>) {
                log_line("Pilot Monitoring: set_dataref_float target is unavailable: " + dataref_name);
            } else {
                log_line("Pilot Monitoring: set_dataref_boolean target is unavailable: " + dataref_name);
            }
            return;
        }
        if (XPLMCanWriteDataRef(dataref) == 0) {
            log_line("Pilot Monitoring: dataref target is read-only: " + dataref_name);
            return;
        }
        if constexpr (std::is_same_v<T, SetIntegerDatarefAction>) XPLMSetDatai(dataref, value.value);
        else if constexpr (std::is_same_v<T, SetFloatDatarefAction>) XPLMSetDataf(dataref, value.value);
        else if constexpr (std::is_same_v<T, SetBooleanDatarefAction>) XPLMSetDatai(dataref, value.value ? 1 : 0);
        }
    }, action);
}

float process_voice_results(float, float, int, void*) {
    if (!g_voice_service) {
        return 0.25F;
    }

    while (const auto result = g_voice_service->pop_result()) {
        if (!result->recognized) {
            log_message("Pilot Monitoring: ");
            log_message(result->text.c_str());
            log_message("\n");
            continue;
        }

        log_message("Pilot Monitoring: transcript: \"");
        log_message(result->text.c_str());
        log_message("\"\n");

        if (!g_execution_engine) {
            log_message("Pilot Monitoring: execution engine is unavailable.\n");
            continue;
        }
        std::string error;
        const auto actions = g_execution_engine->handle_event(Event::transcript_event(result->text), error);
        g_voice_service->set_grammar(g_execution_engine->grammar());
        if (!error.empty()) { log_line("Pilot Monitoring: script error: " + error); continue; }
        for (const auto& action : actions) execute_action(action);
    }
    if (g_execution_engine) {
        std::string error;
        const auto actions = g_execution_engine->handle_event(Event::tick_event(0.1F), error);
        g_voice_service->set_grammar(g_execution_engine->grammar());
        if (!error.empty()) log_line("Pilot Monitoring: script error: " + error);
        for (const auto& action : actions) execute_action(action);
    }
    return 0.1F;
}

float load_aircraft_commands(float, float, int, void*) {
    if (!g_voice_service) {
        return 0.0F;
    }

    std::string error_message;
    auto engine = std::make_unique<ExecutionEngine>(&g_dataref_host, [](std::string_view message) {
        log_line("Pilot Monitoring: " + std::string(message));
    });
    const auto commands = aircraft_commands_path(error_message);
    if (commands.empty() || !engine->load_from_file(commands, error_message)) {
        if (error_message != g_last_aircraft_load_error) {
            log_line("Pilot Monitoring: waiting for aircraft commands: " + error_message);
            g_last_aircraft_load_error = error_message;
        }
        return 0.25F;
    }

    g_execution_engine = std::move(engine);
    g_last_aircraft_load_error.clear();
    log_grammar(g_execution_engine->grammar_text());
    g_voice_service->set_grammar(g_execution_engine->grammar());
    log_message("Pilot Monitoring: aircraft commands loaded.\n");
    return 0.0F;
}

void handle_menu(void*, void* item_ref) {
    if (item_ref == kMenuActionReloadPlugins) {
        log_message("Pilot Monitoring: reloading all plug-ins.\n");
        XPLMReloadPlugins();
    }
}

} // namespace

PLUGIN_API int XPluginStart(char* out_name, char* out_signature, char* out_description) {
    std::strcpy(out_name, kPluginName);
    std::strcpy(out_signature, kPluginSignature);
    std::strcpy(out_description, kPluginDescription);

    g_ptt_command = XPLMCreateCommand(
        kPttCommandName,
        "Pilot Monitoring: hold to capture a voice command");
    XPLMRegisterCommandHandler(g_ptt_command, handle_ptt_command, 1, nullptr);

    const XPLMMenuID plugins_menu = XPLMFindPluginsMenu();
    const int menu_item = XPLMAppendMenuItem(plugins_menu, "Pilot Monitoring", nullptr, 1);
    g_menu = XPLMCreateMenu("Pilot Monitoring", plugins_menu, menu_item, handle_menu, nullptr);
    XPLMAppendMenuItem(g_menu, "Reload all plug-ins", kMenuActionReloadPlugins, 1);

    XPLMCreateFlightLoop_t aircraft_loop_params{};
    aircraft_loop_params.structSize = sizeof(aircraft_loop_params);
    aircraft_loop_params.phase = xplm_FlightLoop_Phase_AfterFlightModel;
    aircraft_loop_params.callbackFunc = load_aircraft_commands;
    g_aircraft_flight_loop = XPLMCreateFlightLoop(&aircraft_loop_params);

    log_message("Pilot Monitoring: plugin started.\n");
    log_message("Pilot Monitoring: whisper.cpp linked: ");
    log_message(whisper_print_system_info());
    log_message("\n");
    return 1;
}

PLUGIN_API void XPluginStop() {
    if (g_flight_loop_registered) {
        XPLMUnregisterFlightLoopCallback(process_voice_results, nullptr);
        g_flight_loop_registered = false;
    }
    if (g_aircraft_flight_loop != nullptr) {
        XPLMDestroyFlightLoop(g_aircraft_flight_loop);
        g_aircraft_flight_loop = nullptr;
    }
    if (g_voice_service) {
        g_voice_service->stop();
        g_voice_service.reset();
    }
    g_execution_engine.reset();
    if (g_menu != nullptr) {
        XPLMDestroyMenu(g_menu);
        g_menu = nullptr;
    }
    if (g_ptt_command != nullptr) {
        XPLMUnregisterCommandHandler(g_ptt_command, handle_ptt_command, 1, nullptr);
        g_ptt_command = nullptr;
    }
    log_message("Pilot Monitoring: plugin stopped.\n");
}

PLUGIN_API int XPluginEnable() {
    g_execution_engine.reset();
    g_last_aircraft_load_error.clear();
    g_voice_service = std::make_unique<VoiceService>(model_path().string(), grammar_parser::parse_state{});
    g_voice_service->start();
    XPLMRegisterFlightLoopCallback(process_voice_results, 0.1F, nullptr);
    g_flight_loop_registered = true;
    if (g_aircraft_flight_loop != nullptr) {
        XPLMScheduleFlightLoop(g_aircraft_flight_loop, 0.1F, 1);
    }
    log_message("Pilot Monitoring: plugin enabled.\n");
    return 1;
}

PLUGIN_API void XPluginDisable() {
    if (g_flight_loop_registered) {
        XPLMUnregisterFlightLoopCallback(process_voice_results, nullptr);
        g_flight_loop_registered = false;
    }
    if (g_voice_service) {
        g_voice_service->stop();
        g_voice_service.reset();
    }
    g_execution_engine.reset();
    g_last_aircraft_load_error.clear();
    log_message("Pilot Monitoring: plugin disabled.\n");
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID, int message, void* param) {
    if (message != XPLM_MSG_PLANE_LOADED ||
        static_cast<int>(reinterpret_cast<intptr_t>(param)) != 0 ||
        !g_flight_loop_registered || g_aircraft_flight_loop == nullptr) {
        return;
    }

    g_execution_engine.reset();
    g_last_aircraft_load_error.clear();
    XPLMScheduleFlightLoop(g_aircraft_flight_loop, 0.1F, 1);
}
