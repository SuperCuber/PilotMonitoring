#include <cstdlib>
#include <string>
#include <variant>
#include <vector>

#include "test_support.h"

int main() {
    FakeHost host;
    std::vector<std::string> logs;
    ExecutionEngine engine(&host, [&logs](std::string_view message) {
        logs.emplace_back(message);
    });
    const std::string lua = R"lua(
register_handler {
    id = "contact",
    phrases = { { "contact", slot("frequency", "float") } },
    handler = function(slots)
        set_dataref_integer("radio", math.floor(slots.frequency * 1000))
    end,
}
register_handler {
    id = "read",
    phrases = { { "read altitude" } },
    handler = function()
        local value, err = get_dataref_float("altitude")
        if value == nil then error(err) end
        set_dataref_float("seen", value)
    end,
}
register_handler {
    id = "announce",
    phrases = { { "announce" } },
    handler = function()
        print("hello", 42)
        say("checklist complete")
    end,
}
)lua";

    std::string error;
    if (!expect_true(engine.load_from_lua_text(lua, error), "Lua failed: " + error)) return EXIT_FAILURE;

    const auto initial_commands = engine.commands();
    if (!expect_equal(initial_commands.size(), std::size_t{3}, "command inspection count") ||
        !expect_equal(initial_commands[0].id, std::string{"contact"}, "first command id") ||
        !expect_true(initial_commands[0].triggers[0].find("<frequency:float>") != std::string::npos,
                     "command trigger does not describe slot") ||
        !expect_true(engine.top_level_grammar_text().find("float_slot") != std::string::npos,
                     "top-level grammar does not include float slot")) return EXIT_FAILURE;

    auto actions = engine.handle_event(Event::transcript_event("CONTACT 121.5"), error);
    if (!expect_true(error.empty(), "contact failed: " + error) ||
        !expect_equal(actions.size(), std::size_t{1}, "contact action count")) return EXIT_FAILURE;
    const auto* radio = std::get_if<SetIntegerDatarefAction>(&actions[0]);
    if (!expect_true(radio != nullptr, "contact action type was not SetIntegerDataref")) return EXIT_FAILURE;
    if (!expect_equal(radio->dataref, std::string{"radio"}, "contact dataref") ||
        !expect_equal(radio->value, 121500, "contact frequency")) return EXIT_FAILURE;

    host.floats["altitude"] = 1200.0F;
    actions = engine.handle_event(Event::transcript_event("read altitude"), error);
    if (!expect_equal(actions.size(), std::size_t{1}, "read action count")) return EXIT_FAILURE;
    if (!expect_true(logs.size() >= 2 && logs[logs.size() - 2] == "get_dataref_float(\"altitude\") -> 1200",
                     "dataref read was not logged with its return value")) return EXIT_FAILURE;
    const auto* seen = std::get_if<SetFloatDatarefAction>(&actions[0]);
    if (!expect_true(seen != nullptr, "read action type was not SetFloatDataref")) return EXIT_FAILURE;
    if (!expect_equal(seen->dataref, std::string{"seen"}, "read destination") ||
        !expect_equal(seen->value, 1200.0F, "read value")) return EXIT_FAILURE;

    actions = engine.handle_event(Event::transcript_event("contact 118.7, read altitude"), error);
    if (!expect_true(error.empty(), "multiple commands failed: " + error) ||
        !expect_equal(actions.size(), std::size_t{2}, "multiple command action count")) return EXIT_FAILURE;
    const auto* chained_radio = std::get_if<SetIntegerDatarefAction>(&actions[0]);
    const auto* chained_seen = std::get_if<SetFloatDatarefAction>(&actions[1]);
    if (!expect_true(chained_radio != nullptr, "first chained action type") ||
        !expect_equal(chained_radio->value, 118700, "first chained action value") ||
        !expect_true(chained_seen != nullptr, "second chained action type") ||
        !expect_equal(chained_seen->value, 1200.0F, "second chained action value")) return EXIT_FAILURE;

    if (!expect_equal(engine.handle_event(Event::tick_event(0.1F), error).size(), std::size_t{0}, "tick action count")) return EXIT_FAILURE;

    actions = engine.handle_event(Event::transcript_event("announce"), error);
    if (!expect_true(error.empty(), "announce failed: " + error) ||
        !expect_equal(actions.size(), std::size_t{1}, "announce action count")) return EXIT_FAILURE;
    const auto* speech = std::get_if<SpeakAction>(&actions[0]);
    if (!expect_true(speech != nullptr, "say action type") ||
        !expect_equal(speech->message, std::string{"checklist complete"}, "say action message")) return EXIT_FAILURE;

    const std::string sound_lua = R"lua(
register_handler {
    id = "sound",
    phrases = { { "sound" } },
    handler = function(_) play_sound("positive_beep") end,
}
)lua";
    if (!expect_true(engine.load_from_lua_text(sound_lua, error), "sound Lua failed: " + error)) return EXIT_FAILURE;
    actions = engine.handle_event(Event::transcript_event("sound"), error);
    if (!expect_true(error.empty() && actions.size() == 1, "sound action count")) return EXIT_FAILURE;
    const auto* sound = std::get_if<PlaySoundAction>(&actions[0]);
    if (!expect_true(sound != nullptr, "sound action type") ||
        !expect_equal(sound->filename, std::string{"positive_beep"}, "sound filename")) return EXIT_FAILURE;
    actions = engine.handle_event(Event::transcript_event("not a command"), error);
    const auto* negative_sound = actions.size() == 1 ? std::get_if<PlaySoundAction>(&actions[0]) : nullptr;
    if (!expect_true(error.empty(), "unrecognized command failed: " + error) ||
        !expect_true(negative_sound != nullptr, "unrecognized command did not play a sound") ||
        !expect_equal(negative_sound->filename, std::string{"negative_beep"},
                      "unrecognized command sound filename")) return EXIT_FAILURE;

    const std::string invalid_sound_lua = R"lua(
register_handler {
    id = "invalid_sound",
    phrases = { { "invalid sound" } },
    handler = function(_) play_sound("../positive_beep") end,
}
)lua";
    if (!expect_true(engine.load_from_lua_text(invalid_sound_lua, error),
                     "invalid sound Lua failed to load: " + error)) return EXIT_FAILURE;

    const std::string coroutine_lua = R"lua(
register_handler {
    id = "timed",
    phrases = { { "timed" } },
    handler = function()
        say("started")
        wait_ms(150)
        say("finished")
    end,
}
register_handler {
    id = "condition",
    phrases = { { "condition" } },
    handler = function()
        wait_until(function()
            return get_dataref_boolean("ready")
        end)
        say("condition met")
    end,
}
register_handler {
    id = "phrase",
    phrases = { { "phrase" } },
    handler = function()
        say("speak now")
        local slots = wait_for_phrase({ { "set", slot("value", "integer") } })
        say("value " .. slots.value)
    end,
}
register_handler {
    id = "caught",
    phrases = { { "caught" } },
    handler = function()
        local ok = pcall(function()
            wait_for_phrase({ { "yes" } })
        end)
        if not ok then say("cancelled") end
    end,
}
register_handler {
    id = "unhandled",
    phrases = { { "unhandled" } },
    handler = function()
        wait_for_phrase({ { "yes" } })
    end,
}
register_handler {
    id = "after_error",
    phrases = { { "after error" } },
    handler = function()
        say("usable")
    end,
}
)lua";

    if (!expect_true(engine.load_from_lua_text(coroutine_lua, error),
                     "coroutine Lua failed: " + error)) return EXIT_FAILURE;

    actions = engine.handle_event(Event::transcript_event("timed"), error);
    if (!expect_true(error.empty(), "timed start failed: " + error) ||
        !expect_equal(actions.size(), std::size_t{1}, "timed start action count")) return EXIT_FAILURE;
    const auto timed_active = engine.active_coroutine();
    if (!expect_true(timed_active.has_value(), "timed handler was not exposed as active") ||
        !expect_equal(timed_active->command_id, std::string{"timed"}, "timed active command id") ||
        !expect_true(timed_active->reason == ActiveCoroutineInfo::YieldReason::WaitMs,
                     "timed active reason") ||
        !expect_true(timed_active->remaining_ms > 0.0, "timed remaining duration")) return EXIT_FAILURE;
    actions = engine.handle_event(Event::transcript_event("condition"), error);
    if (!expect_true(error.empty(), "transcript during wait failed: " + error) ||
        !expect_equal(actions.size(), std::size_t{0}, "transcript during wait action count")) return EXIT_FAILURE;
    actions = engine.handle_event(Event::tick_event(0.1F), error);
    if (!expect_true(error.empty(), "early timed tick failed: " + error) ||
        !expect_equal(actions.size(), std::size_t{0}, "early timed tick action count")) return EXIT_FAILURE;
    actions = engine.handle_event(Event::tick_event(0.05F), error);
    if (!expect_true(error.empty(), "timed completion failed: " + error) ||
        !expect_equal(actions.size(), std::size_t{1}, "timed completion action count")) return EXIT_FAILURE;
    if (!expect_true(!engine.active_coroutine().has_value(), "completed handler remained active")) return EXIT_FAILURE;

    host.booleans["ready"] = false;
    actions = engine.handle_event(Event::transcript_event("condition"), error);
    if (!expect_true(error.empty(), "condition start failed: " + error) ||
        !expect_equal(actions.size(), std::size_t{0}, "condition start action count")) return EXIT_FAILURE;
    const auto condition_active = engine.active_coroutine();
    if (!expect_true(condition_active.has_value() &&
                     condition_active->reason == ActiveCoroutineInfo::YieldReason::WaitUntil,
                     "condition wait was not exposed")) return EXIT_FAILURE;
    actions = engine.handle_event(Event::tick_event(0.1F), error);
    if (!expect_true(error.empty(), "false condition tick failed: " + error) ||
        !expect_equal(actions.size(), std::size_t{0}, "false condition action count")) return EXIT_FAILURE;
    host.booleans["ready"] = true;
    actions = engine.handle_event(Event::tick_event(0.1F), error);
    if (!expect_true(error.empty(), "true condition tick failed: " + error) ||
        !expect_equal(actions.size(), std::size_t{1}, "true condition action count")) return EXIT_FAILURE;

    actions = engine.handle_event(Event::transcript_event("phrase"), error);
    if (!expect_true(error.empty(), "phrase wait start failed: " + error) ||
        !expect_equal(actions.size(), std::size_t{1}, "phrase wait start action count") ||
        !expect_true(engine.grammar_text().find("integer_slot") != std::string::npos,
                     "phrase grammar was not installed")) return EXIT_FAILURE;
    const auto phrase_active = engine.active_coroutine();
    if (!expect_true(phrase_active.has_value() &&
                     phrase_active->reason == ActiveCoroutineInfo::YieldReason::WaitPhrase &&
                     phrase_active->accepted_grammar.find("integer_slot") != std::string::npos,
                     "phrase wait grammar was not exposed")) return EXIT_FAILURE;
    actions = engine.handle_event(Event::transcript_event("set 42"), error);
    if (!expect_true(error.empty(), "phrase wait completion failed: " + error) ||
        !expect_equal(actions.size(), std::size_t{1}, "phrase wait completion action count") ||
        !expect_true(engine.grammar_text().find("phrase") != std::string::npos,
                     "top-level grammar was not restored")) return EXIT_FAILURE;

    actions = engine.handle_event(Event::transcript_event("caught"), error);
    if (!expect_true(error.empty(), "caught wait start failed: " + error)) return EXIT_FAILURE;
    actions = engine.handle_event(Event::transcript_event("no"), error);
    if (!expect_true(error.empty(), "caught wait should be recoverable: " + error) ||
        !expect_equal(actions.size(), std::size_t{2}, "caught wait action count") ||
        !expect_true(std::get_if<PlaySoundAction>(&actions[0]) != nullptr,
                     "caught wait did not play the negative beep")) return EXIT_FAILURE;

    actions = engine.handle_event(Event::transcript_event("unhandled"), error);
    if (!expect_true(error.empty(), "unhandled wait start failed: " + error)) return EXIT_FAILURE;
    actions = engine.handle_event(Event::transcript_event(""), error);
    negative_sound = actions.size() == 1 ? std::get_if<PlaySoundAction>(&actions[0]) : nullptr;
    if (!expect_true(!error.empty(), "empty transcription did not fail the unhandled wait") ||
        !expect_true(negative_sound != nullptr, "empty transcription did not play a sound") ||
        !expect_equal(negative_sound->filename, std::string{"negative_beep"},
                      "empty transcription sound filename") ||
        !expect_true(engine.grammar_text().find("phrase") != std::string::npos,
                     "grammar was not restored after error")) return EXIT_FAILURE;
    actions = engine.handle_event(Event::transcript_event("after error"), error);
    if (!expect_true(error.empty(), "engine unusable after handler error: " + error) ||
        !expect_equal(actions.size(), std::size_t{1}, "post-error action count")) return EXIT_FAILURE;

    actions = engine.handle_event(Event::transcript_event("timed"), error);
    if (!expect_true(error.empty() && engine.active_coroutine().has_value(),
                     "timed handler did not become active before reload")) return EXIT_FAILURE;
    if (!expect_true(engine.load_from_lua_text(coroutine_lua, error), "reload after active handler failed: " + error) ||
        !expect_true(!engine.active_coroutine().has_value(), "reload did not clear active handler inspection")) return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
