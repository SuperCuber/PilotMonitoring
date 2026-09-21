#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "grammar-parser.h"

struct lua_State;

enum class DatarefType { Integer, Float };

struct DatarefHost {
    virtual ~DatarefHost() = default;
    virtual bool validate_dataref(std::string_view name, DatarefType expected_type,
                                  bool write, std::string& error) = 0;
    virtual std::optional<int> get_integer(std::string_view name, std::string& error) = 0;
    virtual std::optional<float> get_float(std::string_view name, std::string& error) = 0;
    virtual std::optional<bool> get_boolean(std::string_view name, std::string& error) = 0;
};

struct TriggerCommandAction { std::string command; };
struct BeginCommandAction { std::string command; };
struct EndCommandAction { std::string command; };
struct SpeakAction { std::string message; };
struct PlaySoundAction { std::string filename; };
struct SetIntegerDatarefAction { std::string dataref; int value = 0; };
struct SetFloatDatarefAction { std::string dataref; float value = 0.0F; };
struct SetBooleanDatarefAction { std::string dataref; bool value = false; };

using Action = std::variant<
    TriggerCommandAction,
    BeginCommandAction,
    EndCommandAction,
    SpeakAction,
    PlaySoundAction,
    SetIntegerDatarefAction,
    SetFloatDatarefAction,
    SetBooleanDatarefAction>;

struct Event {
    enum class Type { Transcript, Tick };
    Type type = Type::Transcript;
    std::string transcript;
    float delta_seconds = 0.0F;

    static Event transcript_event(std::string text);
    static Event tick_event(float delta);
};

struct CommandInfo {
    std::string id;
    std::vector<std::string> triggers;
};

struct ActiveCoroutineInfo {
    enum class YieldReason { WaitMs, WaitUntil, WaitPhrase };

    std::string command_id;
    YieldReason reason = YieldReason::WaitMs;
    double remaining_ms = 0.0;
    std::string accepted_grammar;
};

class ExecutionEngine {
public:
    struct Impl;
    using LogCallback = std::function<void(std::string_view)>;

    explicit ExecutionEngine(DatarefHost* dataref_host = nullptr, LogCallback logger = {});
    ~ExecutionEngine();

    ExecutionEngine(const ExecutionEngine&) = delete;
    ExecutionEngine& operator=(const ExecutionEngine&) = delete;

    bool load_from_file(const std::filesystem::path& path, std::string& error);
    bool load_from_lua_text(std::string_view source, std::string& error);
    std::vector<Action> handle_event(const Event& event, std::string& error);

    [[nodiscard]] const std::string& grammar_text() const { return grammar_text_; }
    [[nodiscard]] const std::string& top_level_grammar_text() const { return top_level_grammar_text_; }
    [[nodiscard]] const grammar_parser::parse_state& grammar() const { return grammar_; }
    [[nodiscard]] bool failed() const { return failed_; }
    [[nodiscard]] DatarefHost* dataref_host() const { return dataref_host_; }
    [[nodiscard]] std::vector<CommandInfo> commands() const;
    [[nodiscard]] std::vector<std::string> expected_triggers() const;
    [[nodiscard]] std::optional<ActiveCoroutineInfo> active_coroutine() const;
    void set_active_grammar(std::string text, grammar_parser::parse_state grammar);

private:
    void clear_active();
    bool resume_active(int arguments, std::string& error);

    std::unique_ptr<Impl> impl_;
    DatarefHost* dataref_host_ = nullptr;
    std::string grammar_text_;
    std::string top_level_grammar_text_;
    grammar_parser::parse_state grammar_;
    bool failed_ = false;
};
