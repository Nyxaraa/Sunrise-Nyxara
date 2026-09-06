-- Route policy reconstructed from the video and authored volume positions.
return function(m, a)
    local C = {region = 40}
    local steps = {
        {trigger = "READY_ROOM_01_SPAWN_SET_02_PLAYER_TRIGGER_80B3C618", groups = {"ready1_reinforce"}, phase = 1},
        {trigger = "READY_ROOM_01_SPAWN_SET_03_PLAYER_TRIGGER", groups = {"ready1_reinforce"}, phase = 1},
        {trigger = "TUMBLER_ENTRY_PLAYER_TRIGGER", groups = {"tumbler"}, phase = 2},
        {trigger = "DECK_EAST_ENTRY_PLAYER_TRIGGER_80B3C6E8", groups = {"sunburn_east"}, phase = 3, cue = 26},
        {trigger = "DECK_BRIDGE_025_PERCENT_PLAYER_TRIGGER_80B3C6E8", groups = {"sunburn_bridge"}, phase = 4, cue = 27},
        {trigger = "DECK_BRIDGE_100_PERCENT_PLAYER_TRIGGER", groups = {"sunburn_west"}, phase = 5},
        -- The deck exit volume is still outdoors. Preload defenders there, but
        -- begin the indoor checkpoint/dialogue only beyond the far Sunside door.
        {trigger = "DECK_WEST_EXIT_PLAYER_TRIGGER_80B3C6E8", groups = {"ready2_entry"}, phase = 5},
        {trigger = "DIALOG_CINDER_READY_ROOM_02_001_PLAYER_TRIGGER", groups = {"ready2_entry"}, phase = 6, cue = 28},
        {trigger = "READY_ROOM_02_SPAWN_SET_01_PLAYER_TRIGGER", groups = {"ready2_entry"}, phase = 6},
        {trigger = "READY_ROOM_02_SPAWN_SET_02_PLAYER_TRIGGER", groups = {"ready2_reinforce"}, phase = 6},
        {trigger = "READY_ROOM_02_SPAWN_SET_03_PLAYER_TRIGGER", groups = {"ready2_reinforce"}, phase = 6},
        {trigger = "READY_ROOM_02_TO_CHAMBER_PLAYER_TRIGGER_80B3C67D", groups = {"chamber"}, phase = 7, cue = 30},
        {trigger = "CHAMBER_SPAWN_SET_01_PLAYER_TRIGGER", groups = {"chamber"}, phase = 7},
        {trigger = "MEAT_GRINDER_SPAWN_SET_01_PLAYER_TRIGGER", groups = {"meat1"}, phase = 8},
        {trigger = "MEAT_GRINDER_SPAWN_SET_02_PLAYER_TRIGGER", groups = {"meat2"}, phase = 9},
        {trigger = "ASCENT_SPAWN_SET_01_PLAYER_TRIGGER", groups = {"ascent"}, phase = 10, cue = 31},
        {trigger = "OVERLOOK_TO_FOUNDRY_PLAYER_TRIGGER", groups = {"foundry_entry"}, phase = 11, cue = 33},
        {trigger = "FOUNDRY_SPAWN_SET_02_PLAYER_TRIGGER", groups = {"foundry_mid"}, phase = 12},
        {trigger = "FOUNDRY_SPAWN_SET_03_PLAYER_TRIGGER", groups = {"foundry_final"}, phase = 13},
    }
    local extras = {"DECK_EAST_SECRET_PLAYER_TRIGGER", "ASCENT_SPAWN_SET_02_PLAYER_TRIGGER",
        "ASCENT_SPAWN_SET_03_PLAYER_TRIGGER", "ASCENT_SPAWN_SET_04_PLAYER_TRIGGER",
        "CHUTE_FIRST_WINDOW_PLAYER_TRIGGER", "APEX_GOTO_001_DIALOG_PLAYER_TRIGGER",
        "DIALOG_CINDER_SUNBURN_001_PLAYER_TRIGGER", "DIALOG_CINDER_READY_ROOM_02_002_PLAYER_TRIGGER",
        "CINDER_FOUNDRY_001_DIALOG_PLAYER_TRIGGER"}
    local current_groups = {{"ready1_entry", "ready1_reinforce"}, {"tumbler"}, {"sunburn_east"},
        {"sunburn_bridge"}, {"sunburn_west"}, {"ready2_entry", "ready2_reinforce"}, {"chamber"},
        {"meat1"}, {"meat2"}, {"ascent"}, {"foundry_entry", "foundry_mid", "foundry_final"}}
    local function phase(s) return s:variable("ember.cinder.phase") or 0 end
    local function set(c, p) c:set_variable("ember.cinder.phase", p) end
    local function retreat(c, s, area, letter)
        local group = area:lower() .. "_retreat_" .. letter:lower()
        if a.groups[group]:enter(c, s) then a.scene(c, area .. "_SQUAD_RETREAT_INSTANCE_" .. letter .. "_PREFAB_RETREAT_SCENE") end
    end
    local function arm(c, minimum)
        for _, row in ipairs(steps) do if row.phase >= (minimum or 0) then a.slot(c, row.trigger):fire_trigger{} end end
        for _, name in ipairs(extras) do a.slot(c, name):fire_trigger{} end
    end
    local function foundry_doors(c, open, snap)
        for _, name in ipairs({"FOUNDRY_HATCH_DOOR_LEFT_DEVICE", "FOUNDRY_HATCH_DOOR_RIGHT_DEVICE",
            "FOUNDRY_RAMROD_LEFT_DEVICE", "FOUNDRY_RAMROD_RIGHT_DEVICE", "FOUNDRY_WARNING_LIGHT_DEVICE"}) do
            a.device(c, name, open, snap)
        end
    end
    function C.enter(c, s)
        if phase(s) > 0 then return end
        set(c, 1)
        a.objects(c, {"CORE_WEAPON_LASER_BEAM_OBJECT_80B3C22D"}, true)
        -- This is the entrance to Ready Room 1; its defenders are beyond the hatch.
        a.device(c, "TUMBLER_HATCH_DOOR_DEVICE", true)
        a.slot(c, "TUMBLER_HATCH_DOOR_KLAXON_AUDIO_SEQUENCE"):play_sequence{}
        a.device(c, "READY_ROOM_01_WINDOW_CONSOLE_DEVICE", false, true)
        foundry_doors(c, false, true)
        arm(c)
        -- Exact 60/118 volume backing DECK_WHOLE_PLAYER_TRIGGER in 80B3C6E8.
        a.effect(c, s, "DECK_HEAT_SHIMMER_HOP_ON", "FOUNDRY_THERMAL_DOT_OBJECT_FILTER",
            {players = true, inside = a.slot(c, "SLOT_0076_80B3C6E8")}, true)
        for _, name in ipairs({"WEAPON_DOWN", "NO_COMBAT_ABILITIES"}) do
            a.effect(c, s, name .. "_HOP_ON", name .. "_OBJECT_FILTER",
                {players = true, inside = a.slot(c, "TV_CHUTE_MOTION_BLUR_TRIGGER_VOLUME")}, true)
        end
        a.spawn(c, s, {"ready1_entry"})
        a.cue(c, s, 19)
        C.guidance(c, s)
    end
    local function advance(c, s, row)
        local previous = phase(s)
        if row.phase < previous then return end
        -- Spatial fallback catches a skipped narrow volume within this room, never a completed room.
        if row.phase > previous then
            set(c, row.phase)
            if row.phase == 6 then a.checkpoint(c, 40, 0x82328D63, "ready2") end
            if row.phase >= 11 and previous < 11 then a.checkpoint(c, 40, 0x782CAF4C, "foundry") end
        end
        if row.phase == 6 then a.spawn(c, s, {"ready2_entry"}) end
        if row.phase >= 11 and row.phase <= 13 then
            a.spawn(c, s, {"foundry_entry"})
            if row.phase >= 12 then a.spawn(c, s, {"foundry_mid"}) end
        end
        a.spawn(c, s, row.groups)
        if row.phase == 6 then a.darkness(c, s, not a.clear(s, current_groups[6]))
        elseif row.phase >= 11 and row.phase <= 13 then a.darkness(c, s, not a.clear(s, current_groups[11])) end
        if row.cue then a.cue(c, s, row.cue) end
        if row.phase == 3 then
            retreat(c, s, "SUNBURN", "A"); retreat(c, s, "SUNBURN", "B"); retreat(c, s, "SUNBURN", "C")
        end
    end
    function C.trigger(c, s, e)
        for _, row in ipairs(steps) do
            if a.matches(c, e, row.trigger) then
                advance(c, s, row); C.guidance(c, s); return
            end
        end
        if a.matches(c, e, "DECK_EAST_SECRET_PLAYER_TRIGGER") and phase(s) <= 5 then a.spawn(c, s, {"sunburn_secret"}) end
        for index, letter in ipairs({"A", "B", "C"}) do
            if a.matches(c, e, "ASCENT_SPAWN_SET_0" .. (index + 1) .. "_PLAYER_TRIGGER") and phase(s) <= 10 then
                retreat(c, s, "ASCENT", letter)
            end
        end
        if a.matches(c, e, "DIALOG_CINDER_SUNBURN_001_PLAYER_TRIGGER") then a.cue(c, s, 23) end
        if a.matches(c, e, "DIALOG_CINDER_READY_ROOM_02_002_PLAYER_TRIGGER") and phase(s) == 6 then a.cue(c, s, 20) end
        if a.matches(c, e, "CHUTE_FIRST_WINDOW_PLAYER_TRIGGER") and phase(s) >= 14 then a.cue(c, s, 35) end
        if a.matches(c, e, "APEX_GOTO_001_DIALOG_PLAYER_TRIGGER") and phase(s) >= 14 then a.cue(c, s, 36) end
        C.guidance(c, s)
    end
    function C.cleared(c, s, name)
        if name == "ready1_entry" then a.spawn(c, s, {"ready1_reinforce"}) end
        if (name == "ready1_entry" or name == "ready1_reinforce") and a.clear(s, current_groups[1]) then
            a.device(c, "READY_ROOM_01_WINDOW_CONSOLE_DEVICE", true)
            a.spawn(c, s, {"tumbler"})
        end
        if name == "ready2_entry" then a.spawn(c, s, {"ready2_reinforce"}) end
        if (name == "ready2_entry" or name == "ready2_reinforce") and phase(s) == 6 and a.clear(s, current_groups[6]) then a.darkness(c, s, false) end
        if name == "foundry_entry" then a.spawn(c, s, {"foundry_mid"}) end
        if name == "foundry_mid" then a.spawn(c, s, {"foundry_final"}) end
        if phase(s) >= 11 and phase(s) <= 13
            and (name == "foundry_entry" or name == "foundry_mid" or name == "foundry_final")
            and a.clear(s, current_groups[11]) then
            set(c, 14)
            foundry_doors(c, true)
            a.slot(c, "FOUNDRY_HATCH_DOOR_CENTER_KLAXON_AUDIO_SEQUENCE"):play_sequence{}
            a.darkness(c, s, false)
            a.cue(c, s, 34)
        end
        C.guidance(c, s)
    end
    function C.guidance(c, s)
        if s:variable("ember.region") ~= 40 then return end
        local p = phase(s)
        local combat = a.combat(s, current_groups[math.min(p, 11)] or {})
        if p <= 2 then a.directive(c, s, "ECECF63D", "CINDER_DIRECTIVE_SUNBURN_GOTO_NAV_POINT", combat)
        elseif p <= 5 then a.directive(c, s, "D496059B", "CINDER_DIRECTIVE_READY_ROOM_02_GOTO_NAV_POINT", combat)
        elseif p == 6 then a.directive(c, s, combat and "7FF69D75" or "DF93A91C", "CINDER_DIRECTIVE_MEAT_GRINDER_GOTO_NAV_POINT", combat)
        elseif p == 7 then a.directive(c, s, "62E3AEFB", "CINDER_DIRECTIVE_MEAT_GRINDER_GOTO_NAV_POINT", combat)
        elseif p <= 10 then a.directive(c, s, "BAD7D583", "CINDER_DIRECTIVE_FOUNDRY_GOTO_NAV_POINT", combat)
        elseif p <= 13 then a.directive(c, s, "7FF69D75", "FOUNDRY_HATCH_DOOR_CENTER_NAV_POINT", combat)
        else a.directive(c, s, "FF7AB219", "CINDER_DIRECTIVE_CHUTE_GOTO_NAV_POINT", false) end
    end
    function C.resume(c, s)
        if (s:variable("ember.r.furthest") or 0) > 2 then return end
        local p = phase(s)
        a.darkness(c, s, (p == 6 and not a.clear(s, current_groups[6]))
            or (p >= 11 and p <= 13 and not a.clear(s, current_groups[11])))
    end
    function C.reset(c, s, name)
        local first = name == "ready2" and 6 or 11
        a.reset(c, current_groups[first])
        if first == 11 then foundry_doors(c, false, true) end
        a.slot(c, first == 6 and "EMBER_CINDER_READY_ROOM_02_OBJECTIVE" or "EMBER_CINDER_FOUNDRY_OBJECTIVE"):reset_objectives{}
        set(c, first); arm(c, first)
        a.spawn(c, s, {first == 6 and "ready2_entry" or "foundry_entry"})
        a.darkness(c, s, true)
        c:clear_variable("ember.r.guidance"); C.guidance(c, s)
    end
    return C
end
