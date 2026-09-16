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
        say("heading " .. slots.heading)
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

register_handler {
    id = "lineup_checklist",
    phrases = {
        { "line up checklist" },
    },
    handler = function(_)
        say("line up checklist, runway")
        wait_for_phrase({ { "runway", slot("runway", "integer"), "identified" } })
        say("landing lights")
        wait_for_phrase({
            { "landing lights on" },
            { "on" },
            { "set" },
        })
        local landing_lights_on = get_dataref_boolean("sim/cockpit2/switches/landing_lights_on")
        if not landing_lights_on then
            say("negative")
            return
        end
        say("line up checklist complete")
    end,
}
