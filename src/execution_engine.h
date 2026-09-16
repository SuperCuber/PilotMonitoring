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

struct DatarefHost {
    virtual ~DatarefHost() = default;
    virtual std::optional<int> get_integer(std::string_view name, std::string& error) = 0;
    virtual std::optional<float> get_float(std::string_view name, std::string& error) = 0;
    virtual std::optional<bool> get_boolean(std::string_view name, std::string& error) = 0;
};

struct TriggerCommandAction { std::string command; };
struct SpeakAction { std::string message; };
struct SetIntegerDatarefAction { std::string dataref; int value = 0; };
struct SetFloatDatarefAction { std::string dataref; float value = 0.0F; };
struct SetBooleanDatarefAction { std::string dataref; bool value = false; };

using Action = std::variant<
    TriggerCommandAction,
    SpeakAction,
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
    [[nodiscard]] const grammar_parser::parse_state& grammar() const { return grammar_; }
    [[nodiscard]] bool failed() const { return failed_; }
    [[nodiscard]] DatarefHost* dataref_host() const { return dataref_host_; }

private:
    std::unique_ptr<Impl> impl_;
    DatarefHost* dataref_host_ = nullptr;
    std::string grammar_text_;
    grammar_parser::parse_state grammar_;
    bool failed_ = false;
};
