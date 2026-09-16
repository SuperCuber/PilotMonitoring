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

    host.booleans["sim/cockpit2/switches/landing_lights_on"] = true;
    actions = engine.handle_event(Event::transcript_event("line up checklist"), error);
    if (!expect_true(error.empty(), "actual lineup start failed: " + error) ||
        !expect_equal(actions.size(), std::size_t{1}, "actual lineup start action count")) return EXIT_FAILURE;
    const auto* start_message = std::get_if<SpeakAction>(&actions[0]);
    if (!expect_true(start_message != nullptr, "actual lineup start action type") ||
        !expect_equal(start_message->message, std::string{"line up checklist. runway"}, "lineup start message")) return EXIT_FAILURE;

    actions = engine.handle_event(Event::transcript_event("runway 27 identified"), error);
    if (!expect_true(error.empty(), "actual runway response failed: " + error) ||
        !expect_equal(actions.size(), std::size_t{1}, "actual runway response action count")) return EXIT_FAILURE;
    const auto* landing_lights_prompt = std::get_if<SpeakAction>(&actions[0]);
    if (!expect_true(landing_lights_prompt != nullptr, "actual landing lights prompt type") ||
        !expect_equal(landing_lights_prompt->message, std::string{"landing lights"}, "landing lights prompt")) return EXIT_FAILURE;

    actions = engine.handle_event(Event::transcript_event("landing lights on"), error);
    if (!expect_true(error.empty(), "actual landing lights response failed: " + error) ||
        !expect_equal(actions.size(), std::size_t{1}, "actual lineup completion action count")) return EXIT_FAILURE;
    const auto* completion = std::get_if<SpeakAction>(&actions[0]);
    if (!expect_true(completion != nullptr, "actual lineup completion action type") ||
        !expect_equal(completion->message, std::string{"line up checklist complete"}, "lineup completion message")) return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
