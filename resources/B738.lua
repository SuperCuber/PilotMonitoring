local function do_command(command, message)
    trigger_command(command)
    if message then say(message) end
end

local function do_simple(id, command, phrases, message)
    register_handler { id = id, phrases = phrases, handler = function(_) do_command(command, message) end }
end

local function set_float(name, value, message)
    set_dataref_float(name, value)
    if message then say(message) end
end

local function flap_handler(detent, command, minimum_speed, maximum_speed)
    return function(_)
        local airspeed = get_dataref_float("sim/cockpit2/gauges/indicators/airspeed_kts_pilot")
        if airspeed == nil then
            say("unable, speed unavailable")
            return
        end
        if airspeed < minimum_speed then
            say("unable, speed too low")
            return
        end
        if airspeed > maximum_speed then
            say("unable, speed too high")
            return
        end
        say("speed checked, flaps " .. detent)
        trigger_command(command)
    end
end

do_simple("gear_up", "laminar/B738/push_button/gear_up", {
    { "gear up" }, { "raise the gear" }, { "landing gear up" },
}, "gear up")
do_simple("gear_down", "laminar/B738/push_button/gear_down", {
    { "gear down" }, { "lower the gear" }, { "landing gear down" },
}, "gear down")

local flap_commands = {
    -- Minimum maneuvering floor and maximum placard speed, in knots.
    { "up", "flaps_0", 0, 340 },
    { "1", "flaps_1", 190, 250 },
    { "2", "flaps_2", 180, 250 },
    { "5", "flaps_5", 170, 250 },
    { "10", "flaps_10", 160, 210 },
    { "15", "flaps_15", 150, 200 },
    { "25", "flaps_25", 140, 190 },
    { "30", "flaps_30", 130, 175 },
    { "40", "flaps_40", 120, 162 },
}
for _, flap in ipairs(flap_commands) do
    local detent, command_name, minimum_speed, maximum_speed = flap[1], flap[2], flap[3], flap[4]
    register_handler {
        id = "flaps_" .. command_name,
        phrases = {
            { "flaps " .. detent },
            { "set flaps " .. detent },
            { "flap " .. detent },
        },
        handler = flap_handler(detent, "laminar/B738/push_button/" .. command_name, minimum_speed, maximum_speed),
    }
end

do_simple("speedbrake_arm", "sim/flight_controls/speed_brakes_arm_toggle", {{ "speedbrake arm" }, { "arm the speedbrake" }}, nil)
do_simple("speedbrake_retract", "sim/flight_controls/speed_brakes_up_all", {{ "speedbrake up" }, { "retract speedbrake" }}, nil)
do_simple("speedbrake_extend", "sim/flight_controls/speed_brakes_down_all", {{ "speedbrake down" }, { "extend speedbrake" }}, nil)
do_simple("parking_brake", "laminar/B738/push_button/park_brake_on_off", {{ "parking brake" }, { "park brake" }}, nil)

local modes = {
    { "cmd a", "laminar/B738/autopilot/cmd_a_press" },
    { "cmd b", "laminar/B738/autopilot/cmd_b_press" },
    { "flight director", "laminar/B738/autopilot/flight_director_toggle" },
    { "lnav", "laminar/B738/autopilot/lnav_press" },
    { "vnav", "laminar/B738/autopilot/vnav_press" },
    { "approach", "laminar/B738/autopilot/app_press" },
    { "vor loc", "laminar/B738/autopilot/vorloc_press" },
    { "heading select", "laminar/B738/autopilot/hdg_sel_press" },
    { "level change", "laminar/B738/autopilot/lvl_chg_press" },
    { "n one", "laminar/B738/autopilot/n1_press" },
    { "speed mode", "laminar/B738/autopilot/speed_press" },
    { "vertical speed", "laminar/B738/autopilot/vs_press" },
    { "autopilot disconnect", "laminar/B738/autopilot/disconnect_button" },
}
for _, mode in ipairs(modes) do
    local phrase, command = mode[1], mode[2]
    do_simple("mode_" .. phrase:gsub(" ", "_"), command, {
        { phrase }, { "select " .. phrase }, { "set " .. phrase },
    }, phrase)
end

register_handler {
    id = "set_heading",
    phrases = {{ "heading", slot("heading", "integer") }, { "set heading", slot("heading", "integer") }},
    handler = function(slots) set_float("laminar/B738/autopilot/mcp_hdg_dial", slots.heading, "heading " .. slots.heading) end,
}
register_handler {
    id = "set_altitude",
    phrases = {{ "altitude", slot("altitude", "integer") }, { "set altitude", slot("altitude", "integer") }},
    handler = function(slots) set_float("laminar/B738/autopilot/mcp_alt_dial", slots.altitude, "altitude " .. slots.altitude) end,
}
register_handler {
    id = "set_speed",
    phrases = {{ "speed", slot("speed", "integer") }, { "set speed", slot("speed", "integer") }},
    handler = function(slots) set_float("laminar/B738/autopilot/mcp_speed_dial_kts", slots.speed, "speed " .. slots.speed) end,
}
register_handler {
    id = "set_nav1_course",
    phrases = {{ "course", slot("course", "integer") }, { "set course", slot("course", "integer") }, { "nav one course", slot("course", "integer") }},
    handler = function(slots) set_float("laminar/B738/autopilot/course_pilot", slots.course, "course " .. slots.course) end,
}

local function radio_handler(standby_ref, swap_command, label)
    return function(slots)
        local frequency = slots.frequency
        if frequency < 100 then frequency = frequency + 100 end
        set_dataref_integer(standby_ref, math.floor(frequency * 1000))
        trigger_command(swap_command)
        say(label .. " " .. frequency)
    end
end
register_handler {
    id = "com1_frequency",
    phrases = {{ "com one", slot("frequency", "float") }, { "set com one", slot("frequency", "float") }},
    handler = radio_handler("sim/cockpit2/radios/actuators/com1_standby_frequency_hz_833", "sim/radios/com1_standy_flip", "com one"),
}
register_handler {
    id = "nav1_frequency",
    phrases = {{ "nav one", slot("frequency", "float") }, { "set nav one", slot("frequency", "float") }},
    handler = radio_handler("sim/cockpit2/radios/actuators/nav1_standby_frequency_hz", "sim/radios/nav1_standy_flip", "nav one"),
}

local silent_commands = {
    { "landing lights on", "sim/lights/landing_lights_on" }, { "landing lights off", "sim/lights/landing_lights_off" },
    { "taxi lights on", "sim/lights/taxi_lights_on" }, { "taxi lights off", "sim/lights/taxi_lights_off" },
    { "beacon on", "sim/lights/beacon_lights_on" }, { "beacon off", "sim/lights/beacon_lights_off" },
    { "strobe on", "sim/lights/strobe_lights_on" }, { "strobe off", "sim/lights/strobe_lights_off" },
    { "navigation lights on", "sim/lights/nav_lights_on" }, { "navigation lights off", "sim/lights/nav_lights_off" },
    { "apu on", "sim/electrical/APU_on" }, { "apu off", "sim/electrical/APU_off" }, { "apu start", "sim/electrical/APU_start" },
    { "engine one cutoff", "laminar/B738/engine/mixture1_cutoff" }, { "engine one idle", "laminar/B738/engine/mixture1_idle" },
    { "engine two cutoff", "laminar/B738/engine/mixture2_cutoff" }, { "engine two idle", "laminar/B738/engine/mixture2_idle" },
    { "engine one start", "laminar/B738/rotary/eng1_start_grd" }, { "engine two start", "laminar/B738/rotary/eng2_start_grd" },
    { "bleed air one", "laminar/B738/toggle_switch/bleed_air_1" }, { "bleed air two", "laminar/B738/toggle_switch/bleed_air_2" },
    { "apu bleed air", "laminar/B738/toggle_switch/bleed_air_apu" },
}
for _, item in ipairs(silent_commands) do
    do_simple("utility_" .. item[1]:gsub(" ", "_"), item[2], {{ item[1] }, { "set " .. item[1] }}, nil)
end

do_simple("nav1_swap", "sim/radios/nav1_standy_flip", {{ "swap nav one" }, { "nav one swap" }}, nil)
do_simple("transponder_ident", "laminar/B738/push_button/transponder_ident_dn", {{ "transponder ident" }, { "squawk ident" }}, nil)
