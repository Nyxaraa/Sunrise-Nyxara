-- Mineral Processing: video ~05:30–10:15. Gameplay receipts, not video timestamps, advance it.
return function(m, a)
    local P = {region = 56}
    local debris = {"TUMBLER_OBSTRUCTION_OBJECT"}
    for _, letter in ipairs({"A", "B", "C", "D", "E", "F", "G", "H", "I"}) do
        debris[#debris + 1] = "TUMBLER_CHAMBER_PRE_PURGE_ROCK_" .. letter .. "_OBJECT"
    end
    local waves = {"processing_wave1", "processing_wave2", "processing_wave3"}
    local triggers = {"PROCESSING_000_PERCENT_PLAYER_TRIGGER", "PROCESSING_015_PERCENT_PLAYER_TRIGGER",
        "PROCESSING_075_PERCENT_PLAYER_TRIGGER", "LINK_ENTRY_PLAYER_TRIGGER",
        "LINK_DIRECTIVE_TUMBLER_OBSTRUCTION_PLAYER_TRIGGER", "LINK_DIRECTIVE_CONTROL_PLAYER_TRIGGER",
        "LINK_CARRY_OBJECT_PLAYER_TRIGGER", "LINK_DIRECTIVE_TUMBLER_EXIT_PLAYER_TRIGGER",
        "DIALOG_LINK_TUMBLER_001_PLAYER_TRIGGER"}
    local function phase(s) return s:variable("ember.processing.phase") or 0 end
    local function set(c, value) c:set_variable("ember.processing.phase", value) end
    local function hatch(c, number, open, snap)
        a.device(c, "DOOR_HATCH_" .. number .. "_DEVICE", open, snap)
    end
    local carry = require("mission_ember.carry")(a, "processing", "CARRY_OBJECT", "CARRY_RECEPTACLE_INTERACT_OBJECT",
        function(c, s) a.cue(c, s, 13); P.guidance(c, s) end,
        function(c, s)
            if phase(s) >= 3 then return end
            set(c, 3)
            a.checkpoint(c, 56, 0x4B27745D, "processing")
            a.darkness(c, s, true)
            a.effect(c, s, "TUMBLER_DAMAGE_HOP_ON", "TUMBLER_DAMAGE_OBJECT_FILTER",
                {players = true, inside = a.slot(c, "TUMBLER_INTERIOR_SAFETY_TRIGGER_VOLUME")}, true)
            a.cue(c, s, 14)
            a.device(c, "TUMBLER_DOOR_DEVICE", false)
            a.device(c, "CONTROL_CONSOLE_SCREEN_DEVICE", true)
            a.device(c, "LINK_TUMBLER_WARNING_LIGHTING_STATE_DEVICE", true)
            a.slot(c, "PROCESSING_AUDIO_KLAXON_SEQUENCE"):play_sequence{}
            hatch(c, "ONE", true); hatch(c, "TWO", true)
            a.spawn(c, s, {waves[1]})
            P.guidance(c, s)
        end)
    function P.enter(c, s)
        if phase(s) > 0 then return end
        set(c, 1)
        a.objects(c, debris, true)
        a.objects(c, {"MERCURY_HOLOGRAM_OBJECT", "DEVICE_DOOR_HATCH_ONE_OBJECT", "DEVICE_DOOR_HATCH_TWO_OBJECT",
            "DEVICE_DOOR_HATCH_THREE_OBJECT", "DEVICE_DOOR_HATCH_FOUR_OBJECT",
            "CORE_WEAPON_LASER_BEAM_OBJECT_80B3C8F7"}, true)
        a.enable_device(c, "CORE_WEAPON_LASER_BEAM_DEVICE")
        a.device(c, "LINK_ENTRY_DOOR_DEVICE", true)
        a.device(c, "TUMBLER_DOOR_DEVICE", true, true)
        a.device(c, "CONTROL_CONSOLE_SCREEN_DEVICE", false, true)
        a.device(c, "LINK_HOLOGRAM_LIGHTING_STATE_DEVICE", true, true)
        for _, n in ipairs({"ONE", "TWO", "THREE", "FOUR"}) do hatch(c, n, false, true) end
        for _, t in ipairs(triggers) do a.slot(c, t):fire_trigger{} end
        a.spawn(c, s, {"processing_entry"})
        a.cue(c, s, 9)
        P.guidance(c, s)
    end
    local function discover(c, s)
        if phase(s) >= 2 then return end
        set(c, 2)
        carry.start(c, s)
        a.cue(c, s, 10)
    end
    function P.trigger(c, s, e)
        if a.matches(c, e, "LINK_DIRECTIVE_TUMBLER_OBSTRUCTION_PLAYER_TRIGGER")
            or a.matches(c, e, "LINK_DIRECTIVE_CONTROL_PLAYER_TRIGGER")
            or a.matches(c, e, "LINK_CARRY_OBJECT_PLAYER_TRIGGER")
            or a.matches(c, e, "PROCESSING_075_PERCENT_PLAYER_TRIGGER") then discover(c, s) end
        if a.matches(c, e, "LINK_DIRECTIVE_CONTROL_PLAYER_TRIGGER") then a.cue(c, s, 11) end
        if phase(s) == 4 and a.matches(c, e, "LINK_DIRECTIVE_TUMBLER_EXIT_PLAYER_TRIGGER") then a.cue(c, s, 18) end
        P.guidance(c, s)
    end
    function P.cleared(c, s, name)
        if name == "processing_entry" then discover(c, s)
        elseif phase(s) == 3 and name == waves[1] then
            hatch(c, "THREE", true); a.spawn(c, s, {waves[2]})
        elseif phase(s) == 3 and name == waves[2] then
            hatch(c, "FOUR", true); a.spawn(c, s, {waves[3]})
        elseif phase(s) == 3 and name == waves[3] and a.clear(s, waves) then
            set(c, 4)
            a.device(c, "TUMBLER_CHAMBER_LEFT_DEVICE", true)
            a.device(c, "TUMBLER_CHAMBER_RIGHT_DEVICE", true)
            a.objects(c, debris, false)
            a.device(c, "TUMBLER_DOOR_DEVICE", true)
            a.device(c, "LINK_TUMBLER_WARNING_LIGHTING_STATE_DEVICE", false)
            a.darkness(c, s, false)
            a.effect(c, s, "TUMBLER_DAMAGE_HOP_ON", nil, nil, false)
            a.cue(c, s, 17)
        end
        P.guidance(c, s)
    end
    function P.guidance(c, s)
        if s:variable("ember.region") ~= 56 then return end
        local p = phase(s)
        if p == 1 then a.directive(c, s, "883C188D", "LINK_DIRECTIVE_CONTROL_INTERACT_NAV_POINT")
        elseif p == 2 then a.directive(c, s, s:variable("ember.carry.processing.notified") and "57050F62" or "591B1D88", carry.held(s) and "LINK_DIRECTIVE_CONTROL_INTERACT_NAV_POINT" or "LINK_CARRY_OBJECT_NAV_POINT")
        elseif p == 3 then a.directive(c, s, "03285502", nil)
        elseif p == 4 then a.directive(c, s, "65D5979D", "CINDER_DIRECTIVE_CINDER_GOTO_BUBBLE_NAV_POINT") end
    end
    function P.reset(c, s)
        a.effect(c, s, "TUMBLER_DAMAGE_HOP_ON", nil, nil, false)
        a.reset(c, waves)
        carry.reset(c, s)
        a.slot(c, "EMBER_LINK_PROCESSING_DEFEND_OBJECTIVE"):reset_objectives{}
        a.objects(c, debris, true)
        for _, n in ipairs({"ONE", "TWO", "THREE", "FOUR"}) do hatch(c, n, false, true) end
        a.device(c, "TUMBLER_DOOR_DEVICE", true, true)
        for _, n in ipairs({"TUMBLER_CHAMBER_LEFT_DEVICE", "TUMBLER_CHAMBER_RIGHT_DEVICE",
            "CONTROL_CONSOLE_SCREEN_DEVICE", "LINK_TUMBLER_WARNING_LIGHTING_STATE_DEVICE"}) do a.device(c, n, false, true) end
        set(c, 2); a.darkness(c, s, false); carry.start(c, s)
        c:clear_variable("ember.r.guidance"); P.guidance(c, s)
    end
    P.interaction = carry.interaction
    function P.object(c, s, e) carry.state(c, s, e); P.guidance(c, s) end
    P.timer = carry.timer
    function P.resume(c, s)
        carry.resume(c, s)
        if (s:variable("ember.r.furthest") or 0) <= 1 then a.darkness(c, s, phase(s) == 3) end
    end
    return P
end
