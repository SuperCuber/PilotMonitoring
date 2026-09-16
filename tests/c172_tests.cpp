#include <cstdlib>
#include <string>
#include <variant>

#include "test_support.h"

namespace {

bool load_c172(ExecutionEngine& engine, std::string& error) {
    return expect_true(engine.load_from_file("resources/C172.lua", error),
                       "C172 config failed: " + error);
}

bool tune_radio_test() {
    FakeHost host;
    ExecutionEngine engine(&host);
    std::string error;
    if (!load_c172(engine, error)) return false;

    auto actions = engine.handle_event(Event::transcript_event("contact 123.45"), error);
    if (!expect_true(error.empty(), "regular contact failed: " + error) ||
        !expect_equal(actions.size(), std::size_t{1}, "regular contact action count")) return false;
    const auto* radio = std::get_if<SetIntegerDatarefAction>(&actions[0]);
    if (!expect_true(radio != nullptr, "regular contact action type was not SetIntegerDataref")) return false;
    if (!expect_true(radio->dataref.find("com1_standby") != std::string::npos,
                     "regular contact dataref: invalid value [" + radio->dataref + "]")) return false;
    if (!expect_equal(radio->value, 123450, "regular contact frequency")) return false;

    actions = engine.handle_event(Event::transcript_event("contact 21.50"), error);
    if (!expect_true(error.empty(), "shortened contact failed: " + error) ||
        !expect_equal(actions.size(), std::size_t{1}, "shortened contact action count")) return false;
    const auto* shortened_radio = std::get_if<SetIntegerDatarefAction>(&actions[0]);
    if (!expect_true(shortened_radio != nullptr, "shortened contact action type was not SetIntegerDataref")) return false;
    if (!expect_equal(shortened_radio->value, 121500, "shortened contact frequency")) return false;
    return true;
}

bool lineup_checklist_test() {
    FakeHost host;
    ExecutionEngine engine(&host);
    std::string error;
    if (!load_c172(engine, error)) return false;

    host.booleans["sim/cockpit2/switches/landing_lights_on"] = true;
    auto actions = engine.handle_event(Event::transcript_event("line up checklist"), error);
    if (!expect_true(error.empty(), "lineup start failed: " + error) ||
        !expect_equal(actions.size(), std::size_t{1}, "lineup start action count")) return false;
    const auto* start_message = std::get_if<SpeakAction>(&actions[0]);
    if (!expect_true(start_message != nullptr, "lineup start action type") ||
        !expect_equal(start_message->message, std::string{"line up checklist. runway"}, "lineup start message")) return false;

    actions = engine.handle_event(Event::transcript_event("runway 27 identified"), error);
    if (!expect_true(error.empty(), "runway response failed: " + error) ||
        !expect_equal(actions.size(), std::size_t{1}, "runway response action count")) return false;
    const auto* landing_lights_prompt = std::get_if<SpeakAction>(&actions[0]);
    if (!expect_true(landing_lights_prompt != nullptr, "landing lights prompt type") ||
        !expect_equal(landing_lights_prompt->message, std::string{"landing lights"}, "landing lights prompt")) return false;

    actions = engine.handle_event(Event::transcript_event("landing lights on"), error);
    if (!expect_true(error.empty(), "landing lights response failed: " + error) ||
        !expect_equal(actions.size(), std::size_t{1}, "lineup completion action count")) return false;
    const auto* completion = std::get_if<SpeakAction>(&actions[0]);
    if (!expect_true(completion != nullptr, "lineup completion action type") ||
        !expect_equal(completion->message, std::string{"line up checklist complete"}, "lineup completion message")) return false;

    host.booleans["sim/cockpit2/switches/landing_lights_on"] = false;
    actions = engine.handle_event(Event::transcript_event("line up checklist"), error);
    if (!expect_true(error.empty(), "failed lineup start failed: " + error) ||
        !expect_equal(actions.size(), std::size_t{1}, "failed lineup start action count")) return false;
    actions = engine.handle_event(Event::transcript_event("runway 27 identified"), error);
    if (!expect_true(error.empty(), "failed runway response failed: " + error) ||
        !expect_equal(actions.size(), std::size_t{1}, "failed runway response action count")) return false;
    actions = engine.handle_event(Event::transcript_event("landing lights on"), error);
    if (!expect_true(error.empty(), "failed landing lights response failed: " + error) ||
        !expect_equal(actions.size(), std::size_t{1}, "failed lineup completion action count")) return false;
    const auto* failure = std::get_if<SpeakAction>(&actions[0]);
    if (!expect_true(failure != nullptr, "failed lineup action type") ||
        !expect_equal(failure->message, std::string{"negative"}, "failed lineup message")) return false;
    return true;
}

bool run_subtest(const std::string& name) {
    if (name == "tune_radio") return tune_radio_test();
    if (name == "lineup_checklist") return lineup_checklist_test();
    return expect_true(false, "unknown C172 subtest: " + name);
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 1) {
        return tune_radio_test() && lineup_checklist_test() ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    for (int index = 1; index < argc; ++index) {
        if (!run_subtest(argv[index])) return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
