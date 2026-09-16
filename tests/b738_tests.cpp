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
    if (!expect_true(error.empty() && actions.size() == 1, "missing airspeed start") ||
        !expect_equal(std::get<PlaySoundAction>(actions[0]).filename, std::string{"negative_beep"}, "missing airspeed beep")) return false;
    actions = engine.handle_event(Event::tick_event(0.31F), error);
    if (!expect_true(error.empty() && actions.size() == 2, "missing airspeed fallback actions")) return false;
    return expect_equal(std::get<TriggerCommandAction>(actions[1]).command,
                        std::string{"laminar/B738/push_button/flaps_5"},
                        "missing airspeed flap command");
}

bool references_test() {
    FakeHost host; ExecutionEngine engine(&host); std::string error;
    if (!load(engine, error)) return false;
    auto actions = engine.handle_event(Event::transcript_event("set heading 270"), error);
    if (!expect_true(error.empty(), "heading failed") || !expect_equal(actions.size(), std::size_t{2}, "heading actions")) return false;
    if (!expect_equal(std::get<SetFloatDatarefAction>(actions[0]).value, 270.0F, "heading value")) return false;
    actions = engine.handle_event(Event::transcript_event("altitude 5000"), error);
    if (!expect_true(error.empty(), "altitude failed") || !expect_equal(actions.size(), std::size_t{2}, "altitude actions")) return false;
    actions = engine.handle_event(Event::transcript_event("flight level 350"), error);
    if (!expect_true(error.empty() && actions.size() == 2, "flight level actions") ||
        !expect_equal(std::get<SetFloatDatarefAction>(actions[0]).value, 35000.0F, "flight level altitude")) return false;
    actions = engine.handle_event(Event::transcript_event("speed 210"), error);
    if (!expect_true(error.empty(), "speed failed") || !expect_equal(actions.size(), std::size_t{2}, "speed actions")) return false;
    actions = engine.handle_event(Event::transcript_event("course 180"), error);
    if (!expect_true(error.empty() && actions.size() == 3, "course actions")) return false;
    if (!expect_equal(std::get<SetFloatDatarefAction>(actions[0]).dataref, std::string{"laminar/B738/autopilot/course_pilot"}, "pilot course dataref") ||
        !expect_equal(std::get<SetFloatDatarefAction>(actions[1]).dataref, std::string{"laminar/B738/autopilot/course_copilot"}, "copilot course dataref")) return false;

    host.floats["sim/flightmodel/position/mag_psi"] = 273.6F;
    actions = engine.handle_event(Event::transcript_event("reset heading"), error);
    if (!expect_true(error.empty() && actions.size() == 2, "reset heading actions")) return false;
    return expect_equal(std::get<SetFloatDatarefAction>(actions[0]).value, 274.0F, "reset heading value");
}

bool automation_test() {
    FakeHost host; ExecutionEngine engine(&host); std::string error;
    if (!load(engine, error)) return false;
    auto actions = engine.handle_event(Event::transcript_event("flight director"), error);
    if (!expect_true(error.empty() && actions.size() == 3, "flight director actions")) return false;
    if (!expect_equal(std::get<TriggerCommandAction>(actions[0]).command, std::string{"laminar/B738/autopilot/flight_director_toggle"}, "pilot flight director") ||
        !expect_equal(std::get<TriggerCommandAction>(actions[1]).command, std::string{"laminar/B738/autopilot/flight_director_fo_toggle"}, "copilot flight director")) return false;

    host.floats["laminar/B738/autopilot/vs_status"] = 1.0F;
    actions = engine.handle_event(Event::transcript_event("vertical speed 1800"), error);
    if (!expect_true(error.empty() && actions.size() == 1, "vertical speed press actions") ||
        !expect_equal(std::get<TriggerCommandAction>(actions[0]).command, std::string{"laminar/B738/autopilot/vs_press"}, "vertical speed press")) return false;
    actions = engine.handle_event(Event::tick_event(0.9F), error);
    if (!expect_true(error.empty() && actions.empty(), "vertical speed delay")) return false;
    actions = engine.handle_event(Event::tick_event(0.11F), error);
    if (!expect_true(error.empty() && actions.size() == 2, "vertical speed target actions") ||
        !expect_equal(std::get<SetFloatDatarefAction>(actions[0]).dataref, std::string{"sim/cockpit/autopilot/vertical_velocity"}, "vertical speed dataref") ||
        !expect_equal(std::get<SetFloatDatarefAction>(actions[0]).value, 1800.0F, "vertical speed value")) return false;

    actions = engine.handle_event(Event::transcript_event("vertical speed minus 1200"), error);
    if (!expect_true(error.empty() && actions.size() == 1, "descent vertical speed press actions")) return false;
    actions = engine.handle_event(Event::tick_event(1.0F), error);
    if (!expect_true(error.empty() && actions.size() == 2, "descent vertical speed actions") ||
        !expect_equal(std::get<SetFloatDatarefAction>(actions[0]).value, -1200.0F, "descent vertical speed value")) return false;

    host.floats["laminar/B738/autopilot/vs_status"] = 0.0F;
    actions = engine.handle_event(Event::transcript_event("vertical speed 900"), error);
    if (!expect_true(error.empty() && actions.size() == 1, "failed vertical speed press actions")) return false;
    actions = engine.handle_event(Event::tick_event(1.0F), error);
    if (!expect_true(error.empty() && actions.size() == 1, "failed vertical speed actions") ||
        !expect_equal(std::get<PlaySoundAction>(actions[0]).filename, std::string{"negative_beep"}, "failed vertical speed beep")) return false;

    actions = engine.handle_event(Event::transcript_event("squawk 1200"), error);
    if (!expect_true(error.empty() && actions.size() == 2, "squawk actions") ||
        !expect_equal(std::get<SetIntegerDatarefAction>(actions[0]).dataref, std::string{"sim/cockpit2/radios/actuators/transponder_code"}, "squawk dataref") ||
        !expect_equal(std::get<SetIntegerDatarefAction>(actions[0]).value, 1200, "squawk code")) return false;

    host.floats["laminar/B738/knob/transponder_pos"] = 1.0F;
    actions = engine.handle_event(Event::transcript_event("set transponder mode charlie"), error);
    if (!expect_true(error.empty() && actions.size() == 3, "mode charlie actions") ||
        !expect_equal(std::get<TriggerCommandAction>(actions[0]).command, std::string{"laminar/B738/knob/transponder_mode_up"}, "mode charlie first step") ||
        !expect_equal(std::get<TriggerCommandAction>(actions[1]).command, std::string{"laminar/B738/knob/transponder_mode_up"}, "mode charlie second step")) return false;

    host.floats["laminar/B738/knob/transponder_pos"] = 3.0F;
    actions = engine.handle_event(Event::transcript_event("transponder ta ra"), error);
    if (!expect_true(error.empty() && actions.size() == 3, "TA/RA actions") ||
        !expect_equal(std::get<TriggerCommandAction>(actions[0]).command, std::string{"laminar/B738/knob/transponder_mode_up"}, "TA/RA first step") ||
        !expect_equal(std::get<TriggerCommandAction>(actions[1]).command, std::string{"laminar/B738/knob/transponder_mode_up"}, "TA/RA second step")) return false;

    host.floats["laminar/B738/knob/transponder_pos"] = 5.0F;
    actions = engine.handle_event(Event::transcript_event("transponder standby"), error);
    return expect_true(error.empty() && actions.size() == 5 &&
                       std::get<TriggerCommandAction>(actions[0]).command == "laminar/B738/knob/transponder_mode_dn" &&
                       std::get<TriggerCommandAction>(actions[3]).command == "laminar/B738/knob/transponder_mode_dn",
                       "standby mode steps");
}

bool run_subtest(const std::string& name) {
    if (name == "radio") return radio_test();
    if (name == "flap") return flap_test();
    if (name == "references") return references_test();
    if (name == "automation") return automation_test();
    return expect_true(false, "unknown B738 subtest: " + name);
}
}

int main(int argc, char** argv) {
    if (argc == 1) {
        return radio_test() && flap_test() && references_test() && automation_test()
                   ? EXIT_SUCCESS
                   : EXIT_FAILURE;
    }
    for (int index = 1; index < argc; ++index) {
        if (!run_subtest(argv[index])) return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
