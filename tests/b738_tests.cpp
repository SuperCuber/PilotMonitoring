#include <cstdlib>
#include <string>
#include <variant>

#include "test_support.h"

namespace {
bool load(ExecutionEngine& engine, std::string& error) {
    return expect_true(engine.load_from_file("resources/B738.lua", error), "B738 load: " + error);
}

bool radio_test() {
    FakeHost host; ExecutionEngine engine(&host); std::string error;
    if (!load(engine, error)) return false;
    auto actions = engine.handle_event(Event::transcript_event("com one 124.85"), error);
    if (!expect_true(error.empty(), "COM1 failed") || !expect_equal(actions.size(), std::size_t{3}, "COM1 actions")) return false;
    if (!expect_equal(std::get<SetIntegerDatarefAction>(actions[0]).dataref, std::string{"sim/cockpit2/radios/actuators/com1_standby_frequency_hz_833"}, "COM1 dataref") ||
        !expect_equal(std::get<SetIntegerDatarefAction>(actions[0]).value, 124850, "COM1 Hz")) return false;
    if (!expect_equal(std::get<TriggerCommandAction>(actions[1]).command, std::string{"sim/radios/com1_standy_flip"}, "COM1 swap")) return false;
    actions = engine.handle_event(Event::transcript_event("nav one 21.50"), error);
    if (!expect_true(error.empty(), "NAV1 failed") || !expect_equal(actions.size(), std::size_t{3}, "NAV1 actions")) return false;
    if (!expect_equal(std::get<SetIntegerDatarefAction>(actions[0]).dataref, std::string{"sim/cockpit2/radios/actuators/nav1_standby_frequency_hz"}, "NAV1 dataref") ||
        !expect_equal(std::get<SetIntegerDatarefAction>(actions[0]).value, 121500, "NAV1 Hz")) return false;
    return expect_equal(std::get<TriggerCommandAction>(actions[1]).command, std::string{"sim/radios/nav1_standy_flip"}, "NAV1 swap");
}

bool flap_test() {
    FakeHost host; ExecutionEngine engine(&host); std::string error;
    if (!load(engine, error)) return false;
    host.floats["sim/cockpit2/gauges/indicators/airspeed_kts_pilot"] = 180.0F;
    auto actions = engine.handle_event(Event::transcript_event("flaps 5"), error);
    if (!expect_true(error.empty(), "safe flap failed") || !expect_equal(actions.size(), std::size_t{2}, "safe flap actions")) return false;
    if (!expect_equal(std::get<SpeakAction>(actions[0]).message, std::string{"speed checked, flaps 5"}, "safe flap readback") ||
        !expect_equal(std::get<TriggerCommandAction>(actions[1]).command, std::string{"laminar/B738/push_button/flaps_5"}, "flap command")) return false;
    host.floats["sim/cockpit2/gauges/indicators/airspeed_kts_pilot"] = 251.0F;
    actions = engine.handle_event(Event::transcript_event("flaps 5"), error);
    if (!expect_true(error.empty(), "unsafe flap errored") || !expect_equal(actions.size(), std::size_t{1}, "unsafe flap actions")) return false;
    if (!expect_equal(std::get<SpeakAction>(actions[0]).message, std::string{"unable, speed too high"}, "unsafe flap warning")) return false;
    host.floats["sim/cockpit2/gauges/indicators/airspeed_kts_pilot"] = 169.0F;
    actions = engine.handle_event(Event::transcript_event("flaps 5"), error);
    if (!expect_true(error.empty() && actions.size() == 1 && std::get<SpeakAction>(actions[0]).message == "unable, speed too low", "low flap speed handling")) return false;
    host.floats.erase("sim/cockpit2/gauges/indicators/airspeed_kts_pilot");
    actions = engine.handle_event(Event::transcript_event("flaps 5"), error);
    return expect_true(error.empty() && actions.size() == 1 && std::get<SpeakAction>(actions[0]).message == "unable, speed unavailable", "missing airspeed handling");
}

bool references_test() {
    FakeHost host; ExecutionEngine engine(&host); std::string error;
    if (!load(engine, error)) return false;
    auto actions = engine.handle_event(Event::transcript_event("set heading 270"), error);
    if (!expect_true(error.empty(), "heading failed") || !expect_equal(actions.size(), std::size_t{2}, "heading actions")) return false;
    if (!expect_equal(std::get<SetFloatDatarefAction>(actions[0]).value, 270.0F, "heading value")) return false;
    actions = engine.handle_event(Event::transcript_event("altitude 5000"), error);
    if (!expect_true(error.empty(), "altitude failed") || !expect_equal(actions.size(), std::size_t{2}, "altitude actions")) return false;
    actions = engine.handle_event(Event::transcript_event("speed 210"), error);
    if (!expect_true(error.empty(), "speed failed") || !expect_equal(actions.size(), std::size_t{2}, "speed actions")) return false;
    actions = engine.handle_event(Event::transcript_event("course 180"), error);
    return expect_true(error.empty() && actions.size() == 2, "course actions");
}

bool run_subtest(const std::string& name) {
    if (name == "radio") return radio_test();
    if (name == "flap") return flap_test();
    if (name == "references") return references_test();
    return expect_true(false, "unknown B738 subtest: " + name);
}
}

int main(int argc, char** argv) {
    if (argc == 1) {
        return radio_test() && flap_test() && references_test()
                   ? EXIT_SUCCESS
                   : EXIT_FAILURE;
    }
    for (int index = 1; index < argc; ++index) {
        if (!run_subtest(argv[index])) return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
