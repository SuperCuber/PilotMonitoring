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
    void set_grammar(grammar_parser::parse_state grammar);
    void stop();
    std::optional<Result> pop_result();

private:
    void worker_main();
    void capture_main();
    void capture_phrase();
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
};
