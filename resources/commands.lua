register_handler {
    id = "set_heading",
    phrases = {
        { "heading", slot("heading", "integer") },
        { "set heading", slot("heading", "integer") },
        { "turn left heading", slot("heading", "integer") },
        { "turn right heading", slot("heading", "integer") },
    },
    handler = function(slots)
        set_dataref_float("sim/cockpit/autopilot/heading_mag", slots.heading)
        print("heading " .. slots.heading)
        say("heading " .. slots.heading)
    end,
}

register_handler {
    id = "annunciator_test",
    phrases = { { "test annunciators" } },
    handler = function()
        trigger_command("sim/annunciator/test_all_annunciators")
    end,
}

register_handler {
    id = "gear_up",
    phrases = { { "gear up" } },
    handler = function()
        trigger_command("")
    end,
}

register_handler {
    id = "tune_radio",
    phrases = {
        { "contact", slot("frequency", "float") },
    },
    handler = function(slots)
        local frequency = slots.frequency
        if frequency < 100 then
            frequency = frequency + 100
        end
        set_dataref_integer("sim/cockpit2/radios/actuators/com1_standby_frequency_hz_833", math.floor(frequency * 1000))
    end,
}
