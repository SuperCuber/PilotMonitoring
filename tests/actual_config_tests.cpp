#include <cstdlib>
#include <string>
#include <variant>

#include "test_support.h"

int main() {
    FakeHost host;
    ExecutionEngine engine(&host);
    std::string error;
    if (!expect_true(engine.load_from_file("resources/commands.lua", error), "actual config failed: " + error)) return EXIT_FAILURE;

    auto actions = engine.handle_event(Event::transcript_event("contact 123.450"), error);
    if (!expect_true(error.empty(), "actual contact failed: " + error) ||
        !expect_equal(actions.size(), std::size_t{1}, "actual contact action count")) return EXIT_FAILURE;
    const auto* radio = std::get_if<SetIntegerDatarefAction>(&actions[0]);
    if (!expect_true(radio != nullptr, "actual contact action type was not SetIntegerDataref")) return EXIT_FAILURE;
    if (!expect_true(radio->dataref.find("com1_standby") != std::string::npos,
                     "actual contact dataref: invalid value [" + radio->dataref + "]")) return EXIT_FAILURE;
    if (!expect_equal(radio->value, 123450, "actual contact frequency")) return EXIT_FAILURE;

    actions = engine.handle_event(Event::transcript_event("contact 21 50"), error);
    if (!expect_true(error.empty(), "actual shorthand contact failed: " + error) ||
        !expect_equal(actions.size(), std::size_t{1}, "actual shorthand contact action count")) return EXIT_FAILURE;
    const auto* shorthand_radio = std::get_if<SetIntegerDatarefAction>(&actions[0]);
    if (!expect_true(shorthand_radio != nullptr, "actual shorthand contact action type was not SetIntegerDataref")) return EXIT_FAILURE;
    if (!expect_equal(shorthand_radio->value, 121500, "actual shorthand contact frequency")) return EXIT_FAILURE;

    actions = engine.handle_event(Event::transcript_event("contact 21 5"), error);
    if (!expect_true(error.empty(), "actual one-digit shorthand contact failed: " + error) ||
        !expect_equal(actions.size(), std::size_t{1}, "actual one-digit shorthand action count")) return EXIT_FAILURE;
    const auto* one_digit_radio = std::get_if<SetIntegerDatarefAction>(&actions[0]);
    if (!expect_true(one_digit_radio != nullptr, "actual one-digit shorthand action type was not SetIntegerDataref")) return EXIT_FAILURE;
    if (!expect_equal(one_digit_radio->value, 121500, "actual one-digit shorthand contact frequency")) return EXIT_FAILURE;

    actions = engine.handle_event(Event::transcript_event("contact 121 50"), error);
    if (!expect_true(error.empty(), "actual paused contact failed: " + error) ||
        !expect_equal(actions.size(), std::size_t{1}, "actual paused contact action count")) return EXIT_FAILURE;
    const auto* paused_radio = std::get_if<SetIntegerDatarefAction>(&actions[0]);
    if (!expect_true(paused_radio != nullptr, "actual paused contact action type was not SetIntegerDataref")) return EXIT_FAILURE;
    if (!expect_equal(paused_radio->value, 121500, "actual paused contact frequency")) return EXIT_FAILURE;

    actions = engine.handle_event(Event::transcript_event("test annunciators"), error);
    if (!expect_true(error.empty(), "actual annunciator failed: " + error) ||
        !expect_equal(actions.size(), std::size_t{1}, "actual annunciator action count")) return EXIT_FAILURE;
    const auto* command = std::get_if<TriggerCommandAction>(&actions[0]);
    if (!expect_true(command != nullptr, "actual annunciator action type was not TriggerCommand")) return EXIT_FAILURE;
    if (!expect_equal(command->command, std::string{"sim/annunciator/test_all_annunciators"}, "annunciator command")) return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
