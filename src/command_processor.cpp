#include "command_processor.h"

#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "json.hpp"

namespace {

using json = nlohmann::json;

std::string normalize_ascii_lower(std::string_view text) {
    std::string normalized;
    normalized.reserve(text.size());

    bool previous_was_space = true;
    for (const unsigned char ch : text) {
        if (std::isspace(ch) != 0) {
            if (!previous_was_space) {
                normalized.push_back(' ');
                previous_was_space = true;
            }
            continue;
        }

        normalized.push_back(static_cast<char>(std::tolower(ch)));
        previous_was_space = false;
    }

    if (!normalized.empty() && normalized.back() == ' ') {
        normalized.pop_back();
    }

    return normalized;
}

std::string normalize_literal(std::string_view text) {
    std::string normalized;
    normalized.reserve(text.size());
    for (const unsigned char ch : text) {
        if (std::isspace(ch) != 0) {
            normalized.push_back(' ');
        } else {
            normalized.push_back(static_cast<char>(std::tolower(ch)));
        }
    }
    return normalized;
}

std::string_view trim_spaces(std::string_view text) {
    std::size_t start = 0;
    while (start < text.size() && text[start] == ' ') {
        ++start;
    }

    std::size_t end = text.size();
    while (end > start && text[end - 1] == ' ') {
        --end;
    }

    return text.substr(start, end - start);
}

std::string escape_gbnf_literal(std::string_view text) {
    std::string escaped;
    escaped.reserve(text.size());

    for (const char ch : text) {
        switch (ch) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped.push_back(ch);
                break;
        }
    }

    return escaped;
}

bool is_slot_type(std::string_view token) {
    return token == "<integer>" || token == "<float>";
}

PhrasePart parse_phrase_part(const json& part_json) {
    if (!part_json.is_string()) {
        throw std::runtime_error("phrase parts must be strings");
    }

    const std::string token = part_json.get<std::string>();
    if (token == "<integer>") {
        return PhrasePart{PhrasePart::Type::IntegerSlot, {}};
    }
    if (token == "<float>") {
        return PhrasePart{PhrasePart::Type::FloatSlot, {}};
    }

    return PhrasePart{PhrasePart::Type::Literal, normalize_literal(token)};
}

CommandActionDefinition parse_action(const json& action_json) {
    if (!action_json.is_object()) {
        throw std::runtime_error("actions must be objects");
    }

    const std::string type = action_json.at("type").get<std::string>();
    if (type == "command_once") {
        CommandActionDefinition action;
        action.type = CommandActionDefinition::Type::CommandOnce;
        action.command = action_json.at("command").get<std::string>();
        return action;
    }

    if (type == "set_dataref") {
        CommandActionDefinition action;
        action.type = CommandActionDefinition::Type::SetDataRef;
        action.dataref = action_json.at("dataref").get<std::string>();
        action.value_type = normalize_ascii_lower(action_json.at("value_type").get<std::string>());
        action.value = action_json.at("value_slot").get<int>();
        if (action.value < 0) {
            throw std::runtime_error("set_dataref value must be a non-negative slot index");
        }
        if (action.value_type != "integer" && action.value_type != "float" && action.value_type != "int_div1000") {
            throw std::runtime_error("set_dataref value_type not recognized");
        }
        return action;
    }

    throw std::runtime_error("unsupported action type: " + type);
}

CommandDefinition parse_command(const json& command_json) {
    if (!command_json.is_object()) {
        throw std::runtime_error("commands must be objects");
    }

    CommandDefinition command;
    command.id = command_json.at("id").get<std::string>();

    const auto& phrases_json = command_json.at("phrases");
    if (!phrases_json.is_array() || phrases_json.empty()) {
        throw std::runtime_error("command must have at least one phrase");
    }

    for (const auto& phrase_json : phrases_json) {
        if (!phrase_json.is_array() || phrase_json.empty()) {
            throw std::runtime_error("phrases must be non-empty arrays");
        }

        std::vector<PhrasePart> phrase;
        for (const auto& part_json : phrase_json) {
            phrase.push_back(parse_phrase_part(part_json));
        }
        command.phrases.push_back(std::move(phrase));
    }

    const auto& actions_json = command_json.at("actions");
    if (!actions_json.is_array() || actions_json.empty()) {
        throw std::runtime_error("command must have at least one action");
    }
    for (const auto& action_json : actions_json) {
        command.actions.push_back(parse_action(action_json));
    }

    return command;
}

std::vector<CommandDefinition> parse_commands(std::string_view json_text) {
    const json root = json::parse(json_text.begin(), json_text.end());
    const int schema_version = root.at("schema_version").get<int>();
    if (schema_version != 1) {
        throw std::runtime_error("unsupported schema_version");
    }

    const auto& commands_json = root.at("commands");
    if (!commands_json.is_array() || commands_json.empty()) {
        throw std::runtime_error("commands must be a non-empty array");
    }

    std::vector<CommandDefinition> commands;
    commands.reserve(commands_json.size());
    for (const auto& command_json : commands_json) {
        commands.push_back(parse_command(command_json));
    }

    return commands;
}

std::string generate_grammar_text(const std::vector<CommandDefinition>& commands) {
    std::ostringstream grammar;
    grammar << "root ::= init command\n";
    grammar << "init ::= \" \"\n\n"; // Important for correct transcription apparently
    grammar << "command ::= (\n";

    bool first_branch = true;
    for (const auto& command : commands) {
        for (const auto& phrase : command.phrases) {
            grammar << (first_branch ? "    " : "  | ");
            first_branch = false;

            bool first_part = true;
            for (const auto& part : phrase) {
                if (!first_part) {
                    grammar << ' ';
                }
                first_part = false;

                if (part.type == PhrasePart::Type::Literal) {
                    grammar << '"' << escape_gbnf_literal(part.text) << '"';
                } else if (part.type == PhrasePart::Type::IntegerSlot) {
                    grammar << "integer_slot";
                } else {
                    grammar << "float_slot";
                }
            }
            grammar << '\n';
        }
    }

    grammar << ")\n\n";
    // Future improvement: support spoken number words here and keep normalizing
    // recognized slot text into canonical digits before command matching.
    grammar << "integer_slot ::= digit+ | digit (\" \" digit)+\n";
    grammar << "float_slot ::= digit+ (\".\" | \"decimal\") digit+\n";
    grammar << "digit ::= [0-9]\n";

    return grammar.str();
}

bool normalize_integer_slot_text(std::string_view text, std::string& normalized_value) {
    if (text.empty()) {
        return false;
    }

    bool all_digits = true;
    std::string digits_only;
    digits_only.reserve(text.size());

    for (const char ch : text) {
        if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
            digits_only.push_back(ch);
            continue;
        }

        all_digits = false;
        if (ch != ' ') {
            return false;
        }
    }

    if (all_digits) {
        normalized_value = digits_only;
        return true;
    }

    if ((text.size() % 2U) == 0U) {
        return false;
    }

    for (std::size_t index = 0; index < text.size(); ++index) {
        const char ch = text[index];
        if ((index % 2U) == 0U) {
            if (std::isdigit(static_cast<unsigned char>(ch)) == 0) {
                return false;
            }
        } else if (ch != ' ') {
            return false;
        }
    }

    normalized_value = digits_only;
    return true;
}

bool normalize_float_slot_text(std::string_view text, std::string& normalized_value) {
    if (text.empty()) {
        return false;
    }

    std::size_t separator_position = std::string_view::npos;
    std::size_t separator_length = 0;

    if (const std::size_t decimal_word_position = text.find("decimal");
        decimal_word_position != std::string_view::npos) {
        separator_position = decimal_word_position;
        separator_length = 7;
    }

    if (const std::size_t dot_position = text.find('.');
        dot_position != std::string_view::npos &&
        (separator_position == std::string_view::npos || dot_position < separator_position)) {
        separator_position = dot_position;
        separator_length = 1;
    }

    if (separator_position == std::string_view::npos) {
        return false;
    }

    const std::string_view whole_part = trim_spaces(text.substr(0, separator_position));
    const std::string_view fractional_part =
        trim_spaces(text.substr(separator_position + separator_length));
    if (whole_part.empty() || fractional_part.empty()) {
        return false;
    }

    std::string normalized_whole_part;
    std::string normalized_fractional_part;
    if (!normalize_integer_slot_text(whole_part, normalized_whole_part) ||
        !normalize_integer_slot_text(fractional_part, normalized_fractional_part)) {
        return false;
    }

    normalized_value = normalized_whole_part + "." + normalized_fractional_part;
    return true;
}

bool try_parse_int(std::string_view text, int& value) {
    if (text.empty()) {
        return false;
    }
    for (const char ch : text) {
        if (std::isdigit(static_cast<unsigned char>(ch)) == 0) {
            return false;
        }
    }
    value = std::stoi(std::string(text));
    return true;
}

bool try_parse_float(std::string_view text, float& value) {
    if (text.empty()) {
        return false;
    }

    bool saw_dot = false;
    for (const char ch : text) {
        if (ch == '.') {
            if (saw_dot) {
                return false;
            }
            saw_dot = true;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(ch)) == 0) {
            return false;
        }
    }

    if (!saw_dot) {
        int integer_value = 0;
        if (!try_parse_int(text, integer_value)) {
            return false;
        }
        value = static_cast<float>(integer_value);
        return true;
    }

    value = std::stof(std::string(text));
    return true;
}

bool match_phrase_parts(
    const std::vector<PhrasePart>& phrase,
    std::size_t part_index,
    std::string_view transcript,
    std::size_t text_index,
    std::vector<std::string>& slots) {
    if (part_index == phrase.size()) {
        return text_index == transcript.size();
    }

    const auto& part = phrase[part_index];
    if (part.type == PhrasePart::Type::Literal) {
        if (transcript.substr(text_index, part.text.size()) != part.text) {
            return false;
        }

        return match_phrase_parts(
            phrase,
            part_index + 1,
            transcript,
            text_index + part.text.size(),
            slots);
    }

    std::size_t slot_end = text_index;
    while (slot_end < transcript.size()) {
        const char ch = transcript[slot_end];
        const bool allowed_digit = std::isdigit(static_cast<unsigned char>(ch)) != 0;
        const bool allowed_integer_char = ch == ' ';
        const bool allowed_float_char =
            allowed_integer_char || ch == '.' || ('a' <= ch && ch <= 'z');
        const bool allowed_char = part.type == PhrasePart::Type::IntegerSlot
            ? (allowed_digit || allowed_integer_char)
            : (allowed_digit || allowed_float_char);
        if (!allowed_char) {
            break;
        }
        ++slot_end;
    }

    for (std::size_t candidate_end = slot_end; candidate_end > text_index; --candidate_end) {
        std::string normalized_slot_value;
        const std::string_view candidate = transcript.substr(text_index, candidate_end - text_index);
        const bool parsed = part.type == PhrasePart::Type::IntegerSlot
            ? normalize_integer_slot_text(candidate, normalized_slot_value)
            : normalize_float_slot_text(candidate, normalized_slot_value);
        if (!parsed) {
            continue;
        }

        slots.push_back(std::move(normalized_slot_value));
        if (match_phrase_parts(phrase, part_index + 1, transcript, candidate_end, slots)) {
            return true;
        }
        slots.pop_back();
    }

    return false;
}

std::optional<MatchedCommand> match_command(
    const std::vector<CommandDefinition>& commands,
    std::string_view transcript) {
    const std::string normalized_transcript = normalize_ascii_lower(transcript);

    for (const auto& command : commands) {
        for (const auto& phrase : command.phrases) {
            std::vector<std::string> slots;
            if (!match_phrase_parts(phrase, 0, normalized_transcript, 0, slots)) {
                continue;
            }

            MatchedCommand matched;
            matched.command_id = command.id;
            matched.slot_values = slots;
            matched.actions.reserve(command.actions.size());

            for (const auto& action_definition : command.actions) {
                ResolvedAction action;
                action.type = action_definition.type == CommandActionDefinition::Type::CommandOnce
                    ? ResolvedAction::Type::CommandOnce
                    : ResolvedAction::Type::SetDataRef;
                action.command = action_definition.command;
                action.dataref = action_definition.dataref;
                action.value_type = action_definition.value_type;

                if (action_definition.type == CommandActionDefinition::Type::SetDataRef) {
                    if (static_cast<std::size_t>(action_definition.value) >= slots.size()) {
                        return std::nullopt;
                    }
                    const std::string& slot_text = slots[static_cast<std::size_t>(action_definition.value)];
                    if (action.value_type == "integer") {
                        if (!try_parse_int(slot_text, action.integer_value)) {
                            return std::nullopt;
                        }
                        action.float_value = static_cast<float>(action.integer_value);
                    } else if (action.value_type == "float" || action.value_type == "int_div1000") {
                        if (!try_parse_float(slot_text, action.float_value)) {
                            return std::nullopt;
                        }
                        action.integer_value = static_cast<int>(action.float_value);
                    } else {
                        return std::nullopt;
                    }
                }

                matched.actions.push_back(std::move(action));
            }

            return matched;
        }
    }

    return std::nullopt;
}

} // namespace

std::optional<CommandProcessor> CommandProcessor::load_from_file(
    const std::filesystem::path& path,
    std::string& error_message) {
    std::ifstream input(path);
    if (!input.is_open()) {
        error_message = "could not open command config: " + path.string();
        return std::nullopt;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return load_from_json_text(buffer.str(), error_message);
}

std::optional<CommandProcessor> CommandProcessor::load_from_json_text(
    std::string_view json_text,
    std::string& error_message) {
    try {
        CommandProcessor processor;
        processor.commands_ = parse_commands(json_text);
        processor.grammar_text_ = generate_grammar_text(processor.commands_);
        processor.grammar_ = grammar_parser::parse(processor.grammar_text_.c_str());
        if (processor.grammar_.rules.empty()) {
            error_message = "failed to parse generated grammar";
            return std::nullopt;
        }
        if (processor.grammar_.symbol_ids.find("root") == processor.grammar_.symbol_ids.end()) {
            error_message = "generated grammar is missing the root rule";
            return std::nullopt;
        }
        return processor;
    } catch (const std::exception& exception) {
        error_message = exception.what();
        return std::nullopt;
    }
}

const std::string& CommandProcessor::grammar_text() const {
    return grammar_text_;
}

const grammar_parser::parse_state& CommandProcessor::grammar() const {
    return grammar_;
}

std::optional<MatchedCommand> CommandProcessor::match(std::string_view transcript) const {
    return match_command(commands_, transcript);
}
