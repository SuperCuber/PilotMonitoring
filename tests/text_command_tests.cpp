#include <cstdlib>
#include <iostream>
#include <string>

#include "command_processor.h"

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << '\n';
        return false;
    }
    return true;
}

} // namespace

int main() {
    const std::string config = R"json(
{
  "schema_version": 1,
  "commands": [
    {
      "id": "set_heading",
      "utterances": [
        ["set heading ", "integer"],
        ["heading ", "integer"],
        ["turn heading ", "integer"]
      ],
      "actions": [
        {
          "type": "set_dataref",
          "dataref": "sim/cockpit/autopilot/heading_mag",
          "value_type": "float",
          "value": 0
        }
      ]
    },
    {
      "id": "set_baro",
      "utterances": [
        ["set baro ", "float"]
      ],
      "actions": [
        {
          "type": "set_dataref",
          "dataref": "sim/cockpit/misc/barometer_setting",
          "value_type": "float",
          "value": 0
        }
      ]
    },
    {
      "id": "annunciator_test",
      "utterances": [
        ["test annunciators"],
        ["test all annunciators"]
      ],
      "actions": [
        {
          "type": "command_once",
          "command": "sim/annunciator/test_all_annunciators"
        }
      ]
    }
  ]
}
)json";

    std::string error_message;
    const auto processor = CommandProcessor::load_from_json_text(config, error_message);
    if (!expect(processor.has_value(), "expected example config to parse: " + error_message)) {
        return EXIT_FAILURE;
    }

    const std::string expected_grammar = R"gbnf(root ::= command

command ::= (
    "set heading " integer_slot
  | "heading " integer_slot
  | "turn heading " integer_slot
  | "set baro " float_slot
  | "test annunciators"
  | "test all annunciators"
)

integer_slot ::= digit+ | digit (" " digit)+
float_slot ::= digit+ ("." | "decimal") digit+
digit ::= [0-9]
)gbnf";

    if (!expect(processor->grammar_text() == expected_grammar, "generated grammar did not match expectation")) {
        return EXIT_FAILURE;
    }

    const auto heading_command = processor->match("TURN HEADING 0 3 0");
    if (!expect(heading_command.has_value(), "expected heading command to match")) {
        return EXIT_FAILURE;
    }
    if (!expect(heading_command->command_id == "set_heading", "expected set_heading command id")) {
        return EXIT_FAILURE;
    }
    if (!expect(heading_command->slot_values.size() == 1 && heading_command->slot_values[0] == "030",
                "expected heading slot to normalize to digits")) {
        return EXIT_FAILURE;
    }
    if (!expect(heading_command->actions.size() == 1, "expected one heading action")) {
        return EXIT_FAILURE;
    }
    if (!expect(heading_command->actions[0].type == ResolvedAction::Type::SetDataRef,
                "expected heading action to be set_dataref")) {
        return EXIT_FAILURE;
    }
    if (!expect(heading_command->actions[0].dataref == "sim/cockpit/autopilot/heading_mag",
                "expected heading action dataref")) {
        return EXIT_FAILURE;
    }
    if (!expect(heading_command->actions[0].float_value == 30.0F, "expected heading float value to be 30")) {
        return EXIT_FAILURE;
    }

    const auto baro_command = processor->match("set baro 2 9 decimal 9 2");
    if (!expect(baro_command.has_value(), "expected baro command to match")) {
        return EXIT_FAILURE;
    }
    if (!expect(baro_command->command_id == "set_baro", "expected set_baro command id")) {
        return EXIT_FAILURE;
    }
    if (!expect(baro_command->slot_values.size() == 1 && baro_command->slot_values[0] == "29.92",
                "expected baro slot to normalize to float text")) {
        return EXIT_FAILURE;
    }
    if (!expect(baro_command->actions.size() == 1, "expected one baro action")) {
        return EXIT_FAILURE;
    }
    if (!expect(baro_command->actions[0].type == ResolvedAction::Type::SetDataRef,
                "expected baro action to be set_dataref")) {
        return EXIT_FAILURE;
    }
    if (!expect(baro_command->actions[0].dataref == "sim/cockpit/misc/barometer_setting",
                "expected baro action dataref")) {
        return EXIT_FAILURE;
    }
    if (!expect(baro_command->actions[0].float_value == 29.92F, "expected baro float value to be 29.92")) {
        return EXIT_FAILURE;
    }

    const auto annunciator_command = processor->match("test all annunciators");
    if (!expect(annunciator_command.has_value(), "expected annunciator command to match")) {
        return EXIT_FAILURE;
    }
    if (!expect(annunciator_command->command_id == "annunciator_test", "expected annunciator command id")) {
        return EXIT_FAILURE;
    }
    if (!expect(annunciator_command->actions.size() == 1, "expected one annunciator action")) {
        return EXIT_FAILURE;
    }
    if (!expect(annunciator_command->actions[0].type == ResolvedAction::Type::CommandOnce,
                "expected annunciator action to be command_once")) {
        return EXIT_FAILURE;
    }
    if (!expect(annunciator_command->actions[0].command == "sim/annunciator/test_all_annunciators",
                "expected annunciator command target")) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
