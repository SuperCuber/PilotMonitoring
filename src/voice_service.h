#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "grammar-parser.h"

struct whisper_context;

class VoiceService {
public:
    struct InputDevice {
        std::string id;
        std::string name;
    };

    struct Result {
        bool recognized;
        std::string text;
    };

    VoiceService(std::string model_path, grammar_parser::parse_state grammar);
    ~VoiceService();

    VoiceService(const VoiceService&) = delete;
    VoiceService& operator=(const VoiceService&) = delete;

    void start();
    void set_listening(bool active);
    [[nodiscard]] bool is_listening() const { return listening_.load(); }
    [[nodiscard]] std::string listening_status() const;
    void set_grammar(grammar_parser::parse_state grammar);
    void stop();
    std::optional<Result> pop_result();

    [[nodiscard]] std::vector<InputDevice> input_devices() const;
    [[nodiscard]] std::string selected_input_device_id() const;
    [[nodiscard]] bool input_device_disconnected() const;
    void refresh_input_devices();
    void set_input_device(std::string id);

private:
    void worker_main();
    void capture_main();
    void capture_phrase();
    void report_device_failure();
    static std::vector<InputDevice> enumerate_input_devices();
    std::vector<float> snapshot_samples();
    std::optional<std::string> transcribe(const std::vector<float>& samples, bool report_errors);
    void push_result(bool recognized, std::string text);

    std::string model_path_;
    mutable std::mutex grammar_mutex_;
    grammar_parser::parse_state grammar_;
    whisper_context* context_ = nullptr; // Created and used only by worker_.
    std::atomic<bool> running_{false};
    std::atomic<bool> listening_{false};
    std::thread worker_;
    std::thread capture_thread_;
    std::mutex wake_mutex_;
    std::condition_variable wake_cv_;
    std::mutex samples_mutex_;
    std::condition_variable samples_cv_;
    std::vector<float> samples_;
    std::mutex results_mutex_;
    std::vector<Result> results_;
    mutable std::mutex device_mutex_;
    std::vector<InputDevice> input_devices_;
    std::string selected_input_device_id_;
    bool input_device_disconnected_ = false;
};
