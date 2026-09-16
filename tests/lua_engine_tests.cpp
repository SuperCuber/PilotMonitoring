#include <cstdlib>
#include <string>
#include <variant>

#include "test_support.h"

int main() {
    FakeHost host;
    ExecutionEngine engine(&host);
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
)lua";

    std::string error;
    if (!expect_true(engine.load_from_lua_text(lua, error), "Lua failed: " + error)) return EXIT_FAILURE;

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
    const auto* seen = std::get_if<SetFloatDatarefAction>(&actions[0]);
    if (!expect_true(seen != nullptr, "read action type was not SetFloatDataref")) return EXIT_FAILURE;
    if (!expect_equal(seen->dataref, std::string{"seen"}, "read destination") ||
        !expect_equal(seen->value, 1200.0F, "read value")) return EXIT_FAILURE;
    if (!expect_equal(engine.handle_event(Event::tick_event(0.1F), error).size(), std::size_t{0}, "tick action count")) return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
