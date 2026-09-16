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

    actions = engine.handle_event(Event::transcript_event("contact 21.50"), error);
    if (!expect_true(error.empty(), "actual shortened float contact failed: " + error) ||
        !expect_equal(actions.size(), std::size_t{1}, "actual shortened float contact action count")) return EXIT_FAILURE;
    const auto* shortened_radio = std::get_if<SetIntegerDatarefAction>(&actions[0]);
    if (!expect_true(shortened_radio != nullptr, "actual shortened float contact action type was not SetIntegerDataref")) return EXIT_FAILURE;
    if (!expect_equal(shortened_radio->value, 121500, "actual shortened float contact frequency")) return EXIT_FAILURE;

    actions = engine.handle_event(Event::transcript_event("contact 121.50"), error);
    if (!expect_true(error.empty(), "actual full float contact failed: " + error) ||
        !expect_equal(actions.size(), std::size_t{1}, "actual full float contact action count")) return EXIT_FAILURE;
    const auto* full_radio = std::get_if<SetIntegerDatarefAction>(&actions[0]);
    if (!expect_true(full_radio != nullptr, "actual full float contact action type was not SetIntegerDataref")) return EXIT_FAILURE;
    if (!expect_equal(full_radio->value, 121500, "actual full float contact frequency")) return EXIT_FAILURE;

    actions = engine.handle_event(Event::transcript_event("test annunciators"), error);
    if (!expect_true(error.empty(), "actual annunciator failed: " + error) ||
        !expect_equal(actions.size(), std::size_t{1}, "actual annunciator action count")) return EXIT_FAILURE;
    const auto* command = std::get_if<TriggerCommandAction>(&actions[0]);
    if (!expect_true(command != nullptr, "actual annunciator action type was not TriggerCommand")) return EXIT_FAILURE;
    if (!expect_equal(command->command, std::string{"sim/annunciator/test_all_annunciators"}, "annunciator command")) return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
