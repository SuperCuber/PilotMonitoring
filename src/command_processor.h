#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "grammar-parser.h"

struct UtterancePart {
    enum class Type {
        Literal,
        IntegerSlot,
    };

    Type type = Type::Literal;
    std::string text;
};

struct CommandActionDefinition {
    enum class Type {
        CommandOnce,
        SetDataRef,
    };

    Type type = Type::CommandOnce;
    std::string command;
    std::string dataref;
    std::string value_type;
    int value = -1;
};

struct CommandDefinition {
    std::string id;
    std::vector<std::vector<UtterancePart>> utterances;
    std::vector<CommandActionDefinition> actions;
};

struct ResolvedAction {
    enum class Type {
        CommandOnce,
        SetDataRef,
    };

    Type type = Type::CommandOnce;
    std::string command;
    std::string dataref;
    std::string value_type;
    int integer_value = 0;
    float float_value = 0.0F;
};

struct MatchedCommand {
    std::string command_id;
    std::vector<int> slot_values;
    std::vector<ResolvedAction> actions;
};

class CommandProcessor {
public:
    static std::optional<CommandProcessor> load_from_file(
        const std::filesystem::path& path,
        std::string& error_message);

    static std::optional<CommandProcessor> load_from_json_text(
        std::string_view json_text,
        std::string& error_message);

    [[nodiscard]] const std::string& grammar_text() const;
    [[nodiscard]] const grammar_parser::parse_state& grammar() const;
    [[nodiscard]] std::optional<MatchedCommand> match(std::string_view transcript) const;

private:
    std::vector<CommandDefinition> commands_;
    std::string grammar_text_;
    grammar_parser::parse_state grammar_;
};
