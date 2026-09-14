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
    return token == "integer";
}

UtterancePart parse_utterance_part(const json& part_json) {
    if (!part_json.is_string()) {
        throw std::runtime_error("utterance parts must be strings");
    }

    const std::string token = part_json.get<std::string>();
    if (is_slot_type(token)) {
        return UtterancePart{UtterancePart::Type::IntegerSlot, {}};
    }

    return UtterancePart{UtterancePart::Type::Literal, normalize_literal(token)};
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
        action.value = action_json.at("value").get<int>();
        if (action.value < 0) {
            throw std::runtime_error("set_dataref value must be a non-negative slot index");
        }
        if (action.value_type != "integer" && action.value_type != "float") {
            throw std::runtime_error("set_dataref value_type must be integer or float");
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

    const auto& utterances_json = command_json.at("utterances");
    if (!utterances_json.is_array() || utterances_json.empty()) {
        throw std::runtime_error("command must have at least one utterance");
    }

    for (const auto& utterance_json : utterances_json) {
        if (!utterance_json.is_array() || utterance_json.empty()) {
            throw std::runtime_error("utterances must be non-empty arrays");
        }

        std::vector<UtterancePart> utterance;
        for (const auto& part_json : utterance_json) {
            utterance.push_back(parse_utterance_part(part_json));
        }
        command.utterances.push_back(std::move(utterance));
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
    if (schema_version != 2) {
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
    grammar << "root ::= command\n\n";
    grammar << "command ::= (\n";

    bool first_branch = true;
    for (const auto& command : commands) {
        for (const auto& utterance : command.utterances) {
            grammar << (first_branch ? "    " : "  | ");
            first_branch = false;

            bool first_part = true;
            for (const auto& part : utterance) {
                if (!first_part) {
                    grammar << ' ';
                }
                first_part = false;

                if (part.type == UtterancePart::Type::Literal) {
                    grammar << '"' << escape_gbnf_literal(part.text) << '"';
                } else {
                    grammar << "integer_slot";
                }
            }
            grammar << '\n';
        }
    }

    grammar << ")\n\n";
    grammar << "integer_slot ::= digit+ | digit_with_spaces\n";
    grammar << "digit_with_spaces ::= digit (\" \" digit)+\n";
    grammar << "digit ::= [0-9]\n\n";
    grammar << "# Future improvement:\n";
    grammar << "# We may want to add spoken integer-word support here as an alternative to digit\n";
    grammar << "# sequences, while still normalizing the final transcript to digits before\n";
    grammar << "# command matching.\n";

    return grammar.str();
}

bool parse_integer_slot_text(std::string_view text, int& value) {
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
        value = std::stoi(digits_only);
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

    value = std::stoi(digits_only);
    return true;
}

bool match_utterance_parts(
    const std::vector<UtterancePart>& utterance,
    std::size_t part_index,
    std::string_view transcript,
    std::size_t text_index,
    std::vector<int>& slots) {
    if (part_index == utterance.size()) {
        return text_index == transcript.size();
    }

    const auto& part = utterance[part_index];
    if (part.type == UtterancePart::Type::Literal) {
        if (transcript.substr(text_index, part.text.size()) != part.text) {
            return false;
        }

        return match_utterance_parts(
            utterance,
            part_index + 1,
            transcript,
            text_index + part.text.size(),
            slots);
    }

    std::size_t slot_end = text_index;
    while (slot_end < transcript.size()) {
        const char ch = transcript[slot_end];
        if (std::isdigit(static_cast<unsigned char>(ch)) == 0 && ch != ' ') {
            break;
        }
        ++slot_end;
    }

    for (std::size_t candidate_end = slot_end; candidate_end > text_index; --candidate_end) {
        int value = 0;
        if (!parse_integer_slot_text(transcript.substr(text_index, candidate_end - text_index), value)) {
            continue;
        }

        slots.push_back(value);
        if (match_utterance_parts(utterance, part_index + 1, transcript, candidate_end, slots)) {
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
        for (const auto& utterance : command.utterances) {
            std::vector<int> slots;
            if (!match_utterance_parts(utterance, 0, normalized_transcript, 0, slots)) {
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
                    action.integer_value = slots[static_cast<std::size_t>(action_definition.value)];
                    action.float_value = static_cast<float>(action.integer_value);
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
