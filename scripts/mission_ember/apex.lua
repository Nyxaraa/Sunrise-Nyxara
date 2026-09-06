-- Light's End: authored access squads, damage-gated reactor, cell delivery and escape.
return function(m, a, ending)
    local A = {region = 0}
    local sides = {"EAST", "WEST"}
    local access = {"access1", "access2", "electron_controllers", "dispenser", "security"}
    local reactor = {"reactor_east_entry", "reactor_west_entry", "reactor_east_reinforce", "reactor_west_reinforce",
        "reactor_east_final", "reactor_west_final", "coffin_east", "coffin_west", "coffin_interior"}
    local triggers = {"ACCESS_DOOR_INNER_PLAYER_TRIGGER", "ACCESS_JUMP_ONE_PLAYER_TRIGGER",
        "ACCESS_JUMP_TWO_PLAYER_TRIGGER", "SECURITY_CENTER_PLAYER_TRIGGER", "SECURITY_DOOR_PLAYER_TRIGGER",
        "SECURITY_LEDGE_PLAYER_TRIGGER", "APEX_DIRECTIVE_REACTOR_GOTO_PLAYER_TRIGGER",
        "APEX_REACTOR_CLAMSHELL_001_DIALOG_START_PLAYER_TRIGGER", "REACTOR_ARM_EAST_ENTRANCE_PLAYER_TRIGGER",
        "REACTOR_MOTHER_BRAIN_ENTRY_PLAYER_TRIGGGER", "DIALOG_APEX_SECURITY_001_TEST_PLAYER_TRIGGER",
        "DIALOG_APEX_SECURITY_002_PLAYER_TRIGGER", "DIALOG_APEX_MOTHER_BRAIN_001_PLAYER_TRIGGER",
        "DIALOG_APEX_MOTHER_BRAIN_002_PLAYER_TRIGGER", "DIALOG_APEX_MOTHER_BRAIN_003_PLAYER_TRIGGER",
        "DIALOG_APEX_MOTHER_BRAIN_004_PLAYER_TRIGGER"}
    local escape_triggers = {"APEX_MOTHER_BRAIN_005_DIALOG_PLAYER_TRIGGER", "APEX_MOTHER_BRAIN_006_DIALOG_PLAYER_TRIGGER",
        "APEX_MOTHER_BRAIN_007_DIALOG_PLAYER_TRIGGER", "APEX_MOTHER_BRAIN_008_DIALOG_PLAYER_TRIGGER",
        "APEX_DIRECTIVE_REACTOR_RAILS_ESCAPE_PLAYER_TRIGGER"}
    -- The authored explosion prefab's four player triggers, west to east along the rails:
    -- set A x -448.75..-438.75, B -403.75..-393.75, C -368.75..-358.75, D -323.75..-313.75,
    -- all spanning y 2967..3002.5 and z 185..212.5. They were never armed, so the sequence
    -- had nothing to advance it and everything the scene did happened at the deposit.
    local explosion_triggers = {}
    for _, set in ipairs({"A", "B", "C", "D"}) do
        explosion_triggers[#explosion_triggers + 1] =
            "EMBER_APEX_EXPLOSION_SEQUENCE_PREFAB_EXPLOSION_SET_" .. set .. "_PLAYER_TRIGGER"
    end
    local function phase(s) return s:variable("ember.apex.phase") or 0 end
    local function set(c, p) c:set_variable("ember.apex.phase", p) end
    local function generation(s) return s:variable("ember.apex.generation") or 1 end
    local function dead(s, target) return s:variable("ember.apex.dead." .. target) == true end
    local function vent_timer(s) return "ember.apex.vents." .. generation(s) end
    local function surge_audio_timer(s) return "ember.apex.surge_audio." .. generation(s) end
    local function lane(c, name, transition, snap)
        a.slot(c, name):transition{transition = c.sdk.device_transitions[transition], snap = snap or false}
    end
    local function unlock(c, name)
        lane(c, name, "unlock"); lane(c, name, "power_on")
    end
    local function bridges(c, extended, snap)
        -- As with the landing bridge, the authored position-0 pose is extended.
        for _, side in ipairs(sides) do
            a.device(c, "CLAMSHELL_TO_COFFIN_" .. side .. "_BRIDGE_DEVICE", not extended, snap)
        end
    end
    local function coffin_doors(c, open, snap)
        for _, side in ipairs(sides) do
            a.device(c, "REACTOR_COFFIN_DOOR_" .. side .. "_DEVICE", open, snap)
            a.device(c, "REACTOR_COFFIN_LIGHT_" .. side .. "_DEVICE", open, snap)
            lane(c, "REACTOR_COFFIN_LIGHT_" .. side .. "_DEVICE", open and "power_on" or "power_off", snap)
        end
        a.device(c, "REACTOR_SHIELD_DEVICE", open, snap)
    end
    -- The weapon has two halves and both are driven.
    --
    -- Objects: the core and ring models carry solid geometry with full LOD tables and are the
    -- structure the weapon is built from, so they are placed once and never taken back out --
    -- removing them deletes the beam and everything around it. The laser model carries the
    -- emitter's effect components and no geometry LODs, so it is the beam itself and its
    -- presence is what has been observed to make the beam appear and disappear.
    --
    -- Devices: `SPECOPS_APEX_RING_LASER_DEVICE` and `..._RING_DEVICE` are the authored lanes of
    -- those same placed objects, exactly as the Mercury lever's type-23 device animates the
    -- type-4 object it belongs to. Their closed vocabulary is position, power and lock. They are
    -- snap-initialized to a baseline and then transitioned without snap, which is the pattern
    -- that made the lever animate. Whether they actually report back is now observable:
    -- `ring_device_sense` in the log carries their native position and power.
    local structure_objects = {"SPECOPS_APEX_RING_CORE_OBJECT", "SPECOPS_APEX_RING_RING_OBJECT"}
    local beam_devices = {"SPECOPS_APEX_RING_LASER_DEVICE", "SPECOPS_APEX_RING_RING_DEVICE"}
    -- These carry the same inverted pose as the landing and clamshell bridges, where the
    -- authored position-0 lane is the driven end rather than the resting one. Confirmed in game:
    -- the resting drive rendered as the surge and the driven end as the weapon's normal state,
    -- so resting is `open` and the surge drives to `close`.
    local function beam_pose(c, surging, snap)
        for _, name in ipairs(beam_devices) do a.device(c, name, not surging, snap) end
    end
    -- Firing: the beam is present and both of its devices are powered.
    local function beam(c, s, firing, snap)
        -- Idempotent: redundant publications would spend intents from the callback's budget.
        if s:variable("ember.apex.beam") == firing then return end
        c:set_variable("ember.apex.beam", firing)
        a.objects(c, {"SPECOPS_APEX_RING_LASER_OBJECT"}, firing)
        for _, name in ipairs(beam_devices) do
            lane(c, name, firing and "power_on" or "power_off", snap)
        end
        if not firing then
            -- Installed but dark: the drive returns to its resting pose with the power.
            c:set_variable("ember.apex.surge", false)
            beam_pose(c, false, snap)
        end
    end
    -- The surge is the authored drive of a firing beam, so it never removes it. If these
    -- devices do move the emitter, this is the lane that shows it.
    local function beam_surge(c, s, on)
        if s:variable("ember.apex.beam") ~= true or s:variable("ember.apex.surge") == on then return end
        c:set_variable("ember.apex.surge", on)
        beam_pose(c, on, false)
    end
    -- `REACTOR_COFFIN_INTERIOR_THERMAL_HOP_ON` and `FOUNDRY_THERMAL_DOT_HOP_ON` both reference
    -- effect resource 80C1D9E0 -- the burn already working in the Foundry. The hot-pipe and
    -- rail-top hop-ons carry 80B82484 and 80C1D389 instead, which is why contact read as a
    -- shock rather than a scorch. Use the authored scorch for the climb pipes. A new revision
    -- removes the previous attachment (native 9EF8A0/9F1F10) before attaching the new filter.
    local function rail_filter(c) return {players = true, inside = a.slot(c, "SLOT_019E")} end
    local function hazards(c, s, mode)
        if mode == "climb" then
            -- The five narrow authored pipe volumes on the way up to the deposit; their heights
            -- track the climb the mother-brain dialogue volumes walk through, from z~172 at
            -- x=-395 up to z~186 at x=-460. Slot 7 is a broad kill volume well below the
            -- walkable route and is deliberately not a pipe.
            --
            -- The filter is the one that lives in the same object as those volumes. Pointing the
            -- apex-object filter at globals-object volumes published cleanly but attached
            -- nothing; this is the pairing that demonstrably delivered contact damage before.
            a.effect(c, s, "REACTOR_COFFIN_INTERIOR_THERMAL_HOP_ON",
                "REACTOR_MOTHER_BRAIN_HOT_PIPES_OBJECT_FILTER_80B3C09F",
                {players = true, inside_any = {
                    a.slot(c, "REACTOR_MOTHER_BRAIN_HOT_PIPES_02_TRIGGER_VOLUME"),
                    a.slot(c, "REACTOR_MOTHER_BRAIN_HOT_PIPES_03_TRIGGER_VOLUME"),
                    a.slot(c, "SLOT_0005_80B3C09F"), a.slot(c, "SLOT_0006_80B3C09F"),
                    a.slot(c, "SLOT_0008_80B3C09F")}}, true)
        elseif mode == "escape" then
            -- Escape already owns SUNBURN_DAMAGE_OBJECT. Adding this rail-wide burn on top
            -- adds damage until the escape-end trigger detaches it. Retire the
            -- climb attachment here and let the native sunburn object own escape damage.
            a.effect(c, s, "REACTOR_COFFIN_INTERIOR_THERMAL_HOP_ON",
                "AOD_REACTOR_RAIL_TOP_OBJECT_FILTER", rail_filter(c), false)
        else
            a.effect(c, s, "REACTOR_COFFIN_INTERIOR_THERMAL_HOP_ON",
                "AOD_REACTOR_RAIL_TOP_OBJECT_FILTER", rail_filter(c), false)
        end
    end
    -- Activate the authored explosion scene once, then arm its own four progress triggers
    -- alongside the escape dialogue volumes so each section detonates as the player reaches it.
    local function arm_escape(c)
        a.scene(c, "EMBER_APEX_EXPLOSION_SEQUENCE_PREFAB_TRIGGERED_EXPLOSIONS_SCENE")
        for _, name in ipairs(explosion_triggers) do a.slot(c, name):fire_trigger{} end
        for _, name in ipairs(escape_triggers) do a.slot(c, name):fire_trigger{} end
    end
    local function doors(c, side, open, snap)
        for _, part in ipairs({"DOOR_A", "DOOR_B", "LIGHT_A", "LIGHT_B", "TARGET"}) do
            local name = "REACTOR_CLAMSHELL_" .. side .. "_" .. part .. "_DEVICE"
            a.device(c, name, open, snap)
            if part == "LIGHT_A" or part == "LIGHT_B" or part == "TARGET" then
                a.slot(c, name):transition{transition = open and c.sdk.device_transitions.power_on
                    or c.sdk.device_transitions.power_off, snap = snap or false}
            end
        end
    end
    local function target(c, s, name)
        a.slot(c, name .. "_TARGET_OBJECT"):set_interactable_object{generation = generation(s)}
        -- Bind on the object's presence receipt: Auth delivery and entity creation
        -- happen on different ticks. A missing/unloaded object is not a kill.
    end
    -- One encounter clock survives the clamshell -> coffin transition. Durations are
    -- reconstructed from the reference's warning, exposure and recovery windows.
    local function cycle(c, s, step, snap)
        c:set_variable("ember.apex.vent_step", step)
        c:set_variable("ember.apex.vents_open", step == "open")
        if step == "warning" then
            beam_surge(c, s, true)
        else
            -- Exposure is the cooling window: stop the surge before moving the shutters.
            beam_surge(c, s, false)
            if phase(s) == 3 then
                for _, side in ipairs(sides) do if not dead(s, side) then doors(c, side, step == "open", snap) end end
            elseif phase(s) == 4 then
                coffin_doors(c, step == "open", snap)
                if step == "open" then a.cue(c, s, 46) end
            end
        end
        c:cancel_timer(surge_audio_timer(s))
        if step == "closed" then
            -- Playtest: requesting the sequence at surge start made its sound land at
            -- cooling-door opening, one six-second window late. Pre-roll audio only;
            -- the verified visual/exposure clock remains unchanged.
            c:start_timer(surge_audio_timer(s), 8000)
        end
        c:start_timer(vent_timer(s), ({closed = 14000, warning = 6000, open = 10000})[step])
    end
    local function initialize_devices(c, s)
        if s:variable("ember.apex.devices_ready") then return end
        c:set_variable("ember.apex.devices_ready", true)
        unlock(c, "SECURITY_DOOR_DEVICE")
        for _, side in ipairs(sides) do
            for _, part in ipairs({"DOOR_A", "DOOR_B"}) do unlock(c, "REACTOR_CLAMSHELL_" .. side .. "_" .. part .. "_DEVICE") end
            unlock(c, "CLAMSHELL_TO_COFFIN_" .. side .. "_BRIDGE_DEVICE")
            lane(c, "REACTOR_COFFIN_DOOR_" .. side .. "_DEVICE", "lock")
        end
        lane(c, "MOTHER_BRAIN_DOOR_DEVICE", "lock")
        unlock(c, "SPECOPS_APEX_RING_LASER_DEVICE")
        unlock(c, "SPECOPS_APEX_RING_RING_DEVICE")
        -- The weapon is firing at the sun from the moment the area comes up. Snap the drive to
        -- its baseline here so every later surge is an animated transition, not a jump.
        beam(c, s, true, true)
        c:set_variable("ember.apex.surge", false)
        beam_pose(c, false, true)
    end
    local function start_reactor(c, s)
        if phase(s) >= 3 then return end
        initialize_devices(c, s)
        set(c, 3)
        a.checkpoint(c, 0, 0xDF59C25C, "reactor")
        a.darkness(c, s, true)
        a.device(c, "SECURITY_DOOR_DEVICE", true)
        for _, side in ipairs(sides) do target(c, s, "REACTOR_CLAMSHELL_" .. side) end
        a.spawn(c, s, {"reactor_east_entry", "reactor_west_entry"})
        a.cue(c, s, 40); cycle(c, s, "closed", true)
        c:start_timer("ember.apex.explain." .. generation(s), 9000)
    end
    local carry = require("mission_ember.carry")(a, "apex", "MOTHER_BRAIN_CARRY_OBJECT", "MOTHER_BRAIN_INTERACT_OBJECT",
        function(c, s) a.cue(c, s, 50); A.guidance(c, s) end,
        function(c, s)
            if phase(s) ~= 5 then return end
            set(c, 6)
            c:start_timer("ember.apex.hazards", 1)
            a.checkpoint(c, 0, 0x45920385, "escape")
            a.device(c, "MOTHER_BRAIN_CONSOLE_DEVICE", true)
            a.device(c, "MOTHER_BRAIN_ENGINE_LEFT_DEVICE", true)
            a.device(c, "MOTHER_BRAIN_ENGINE_RIGHT_DEVICE", true)
            a.objects(c, {"REACTOR_GETAWAY_SHIP_OBJECT", "SUNBURN_DAMAGE_OBJECT"}, true)
            -- Object publication is not an entity-creation receipt. Start its device from
            -- A.object once the ship is present, so the initial movement is not lost.
            c:clear_variable("ember.apex.ship_started")
            -- The weapon is dead once the cell is in: powered off but still installed, so
            -- the beam and everything built around it stay in the world. The previous code
            -- opened both devices here, which left it running through the whole escape.
            beam(c, s, false, true)
            c:cancel_timer(vent_timer(s))
            c:cancel_timer(surge_audio_timer(s))
            a.scene(c, "MOTHER_BRAIN_HOLE_EXPLOSION_SCENE")
            arm_escape(c)
            a.cue(c, s, 51); A.guidance(c, s)
        end)
    function A.enter(c, s)
        if s:variable("ember.apex.restart_pending") then
            c:clear_variable("ember.apex.restart_pending")
            start_reactor(c, s); A.guidance(c, s); return
        end
        if phase(s) > 0 then return end
        set(c, 1)
        c:start_timer("ember.apex.setup." .. generation(s), 1)
        a.device(c, "ACCESS_DOOR_INNER_DEVICE", true)
        a.device(c, "ACCESS_DOOR_OUTER_DEVICE", true)
        a.device(c, "SECURITY_DOOR_DEVICE", false, true)
        a.slot(c, "SECURITY_PLACED_INTERCEPTOR_OBJECT"):set_interactable_object{generation = 1}
        -- The weapon's structure is placed once and is never taken back out of the world.
        a.objects(c, structure_objects, true)
        for _, side in ipairs(sides) do
            doors(c, side, false, true)
            a.device(c, "CLAMSHELL_PIPES_" .. side .. "_DEVICE", false, true)
            a.device(c, "CLAMSHELL_TO_COFFIN_" .. side .. "_BRIDGE_DEVICE", true, true)
            a.device(c, "REACTOR_COFFIN_DOOR_" .. side .. "_DEVICE", false, true)
        end
        a.device(c, "REACTOR_SHIELD_DEVICE", false, true)
        a.device(c, "COFFIN_BUNKER_DOOR_SOUTH_DEVICE", false, true)
        a.device(c, "MOTHER_BRAIN_DOOR_DEVICE", false, true)
        for _, name in ipairs(triggers) do a.slot(c, name):fire_trigger{} end
        a.spawn(c, s, {"access1"}); A.guidance(c, s)
    end
    function A.trigger(c, s, e)
        if phase(s) <= 2 then
            if a.matches(c, e, "ACCESS_JUMP_TWO_PLAYER_TRIGGER") then a.spawn(c, s, {"access2"}) end
            if a.matches(c, e, "SECURITY_CENTER_PLAYER_TRIGGER") or a.matches(c, e, "SECURITY_LEDGE_PLAYER_TRIGGER")
                or a.matches(c, e, "SECURITY_DOOR_PLAYER_TRIGGER") then
                set(c, 2); a.spawn(c, s, access)
                a.device(c, "DISPENSER_MONSTER_CLOSET_LIGHT_DEVICE", true)
                if not s:variable("ember.apex.dispenser") then
                    c:set_variable("ember.apex.dispenser", true)
                    a.slot(c, "DISPENSER_MONSTER_CLOSET_SEQUENCE"):play_sequence{}
                end
                a.cue(c, s, 38)
            end
        end
        if a.matches(c, e, "APEX_DIRECTIVE_REACTOR_GOTO_PLAYER_TRIGGER")
            or a.matches(c, e, "APEX_REACTOR_CLAMSHELL_001_DIALOG_START_PLAYER_TRIGGER")
            or a.matches(c, e, "REACTOR_ARM_EAST_ENTRANCE_PLAYER_TRIGGER") then start_reactor(c, s) end
        -- The explanatory cue follows the arrival line, even if its trigger was crossed in the same tick.
        if phase(s) == 5 and a.matches(c, e, "DIALOG_APEX_MOTHER_BRAIN_001_PLAYER_TRIGGER") then a.cue(c, s, 50) end
        if phase(s) == 6 then
            if a.matches(c, e, "APEX_MOTHER_BRAIN_006_DIALOG_PLAYER_TRIGGER") then a.cue(c, s, 52) end
            if a.matches(c, e, "APEX_MOTHER_BRAIN_007_DIALOG_PLAYER_TRIGGER") then a.cue(c, s, 53) end
            if a.matches(c, e, "APEX_MOTHER_BRAIN_008_DIALOG_PLAYER_TRIGGER") then a.cue(c, s, 54) end
            if a.matches(c, e, "APEX_DIRECTIVE_REACTOR_RAILS_ESCAPE_PLAYER_TRIGGER") then
                set(c, 7); a.darkness(c, s, false)
                hazards(c, s, nil)
                c:cancel_timer("ember.apex.hazards")
                c:cancel_timer(vent_timer(s))
                ending.start(c, s)
            end
        end
        A.guidance(c, s)
    end
    function A.cleared(c, s, name)
        if name == "access1" and phase(s) <= 2 then a.spawn(c, s, {"access2"}) end
        if name == "electron_controllers" and phase(s) <= 2 then
            unlock(c, "SECURITY_DOOR_DEVICE")
            a.device(c, "SECURITY_DOOR_DEVICE", true)
        end
        if phase(s) == 3 or phase(s) == 4 then
            for _, side in ipairs({"east", "west"}) do
                if name == "reactor_" .. side .. "_entry" then a.spawn(c, s, {"reactor_" .. side .. "_reinforce"}) end
                if name == "reactor_" .. side .. "_reinforce" then a.spawn(c, s, {"reactor_" .. side .. "_final"}) end
            end
        end
        A.guidance(c, s)
    end
    local function destroyed(c, s, which)
        if dead(s, which) or (which == "COFFIN" and phase(s) ~= 4)
            or (which ~= "COFFIN" and phase(s) ~= 3) then return end
        c:set_variable("ember.apex.dead." .. which, true)
        if which ~= "COFFIN" then
            doors(c, which, true)
            a.scene(c, "APEX_REACTOR_CLAMSHELL_" .. which .. "_DESTROYED_SCENE")
            a.device(c, "CLAMSHELL_PIPES_" .. which .. "_DEVICE", true)
            a.cue(c, s, dead(s, "EAST") and dead(s, "WEST") and 44 or 43)
            if dead(s, "EAST") and dead(s, "WEST") then
                set(c, 4) -- Keep the current warning/exposure deadline for the central reactor.
                -- Remaining authored waves stay alive and retain their objectives.
                a.spawn(c, s, {"reactor_east_reinforce", "reactor_west_reinforce", "reactor_east_final", "reactor_west_final"})
                c:start_timer("ember.apex.core." .. generation(s), 1)
            end
        else
            set(c, 5)
            c:cancel_timer(vent_timer(s)); c:cancel_timer("ember.apex.explain." .. generation(s))
            c:cancel_timer(surge_audio_timer(s))
            coffin_doors(c, true)
            -- The weapon keeps firing until the cell goes in.
            beam(c, s, true)
            -- Arm the climb scorch now: the pipes burn while the cell is carried up.
            c:start_timer("ember.apex.hazards", 1)
            unlock(c, "MOTHER_BRAIN_DOOR_DEVICE")
            unlock(c, "COFFIN_BUNKER_DOOR_SOUTH_DEVICE")
            a.device(c, "MOTHER_BRAIN_DOOR_DEVICE", true)
            a.device(c, "COFFIN_BUNKER_DOOR_SOUTH_DEVICE", true)
            carry.start(c, s); a.cue(c, s, 48)
        end
        A.guidance(c, s)
    end
    function A.damage(c, s, e)
        if e.revision ~= generation(s) or e.health == nil or e.health ~= e.health or e.health < 0 then return end
        local which
        for _, side in ipairs(sides) do if a.matches(c, e, "REACTOR_CLAMSHELL_" .. side .. "_TARGET_DAMAGE") then which = side end end
        if a.matches(c, e, "REACTOR_COFFIN_TARGET_DAMAGE") then which = "COFFIN" end
        if not which or dead(s, which) or (which == "COFFIN" and phase(s) ~= 4)
            or (which ~= "COFFIN" and phase(s) ~= 3) then return end
        local seen = "ember.apex.healthy." .. which
        if e.health > 0 then c:set_variable(seen, true); return end
        if not s:variable(seen) then return end -- Absent/unloaded targets are never destruction receipts.
        destroyed(c, s, which)
    end
    function A.timer(c, s, e)
        if e.timer_name == surge_audio_timer(s) then
            if s:variable("ember.region") == 0 and s:variable("ember.apex.vent_step") == "closed" then
                if phase(s) == 3 then
                    for _, side in ipairs(sides) do
                        if not dead(s, side) then
                            a.slot(c, "REACTOR_CLAMSHELL_" .. side .. "_ALARM_SEQUENCE"):play_sequence{}
                        end
                    end
                elseif phase(s) == 4 then a.slot(c, "REACTOR_COFFIN_ALARM_SEQUENCE"):play_sequence{} end
            end
            return true
        end
        if e.timer_name == "ember.apex.hazards" then
            -- The climb pipes burn while the cell is being carried up (phase 5), well before
            -- the deposit. Escape (phase 6) uses its separate native sunburn object, so
            -- entering it detaches the climb burn. A checkpoint reset also detaches it.
            if s:variable("ember.region") == 0 then
                local p = phase(s)
                if p == 5 then hazards(c, s, "climb")
                elseif p == 6 then hazards(c, s, "escape")
                else hazards(c, s, nil) end
            end
            return true
        end
        if carry.timer(c, s, e) then return true end
        if e.timer_name == "ember.apex.setup." .. generation(s) then
            if s:variable("ember.region") == 0 then initialize_devices(c, s) end
            return true
        end
        if e.timer_name == "ember.apex.explain." .. generation(s) then
            if phase(s) == 3 and s:variable("ember.region") == 0 then a.cue(c, s, 41); A.guidance(c, s) end
            return true
        end
        if e.timer_name == vent_timer(s) then
            if (phase(s) == 3 or phase(s) == 4) and s:variable("ember.region") == 0 then
                cycle(c, s, ({closed = "warning", warning = "open", open = "closed"})[s:variable("ember.apex.vent_step")] or "closed")
                A.guidance(c, s)
            end
            return true
        end
        if e.timer_name == "ember.apex.core." .. generation(s) and phase(s) == 4 then
            if s:variable("ember.region") ~= 0 or s:variable("ember.apex.core_ready") then return true end
            c:set_variable("ember.apex.core_ready", true)
            bridges(c, true)
            for _, side in ipairs(sides) do unlock(c, "REACTOR_COFFIN_DOOR_" .. side .. "_DEVICE") end
            unlock(c, "REACTOR_SHIELD_DEVICE")
            coffin_doors(c, s:variable("ember.apex.vents_open") == true)
            target(c, s, "REACTOR_COFFIN")
            a.spawn(c, s, {"coffin_east", "coffin_west", "coffin_interior"})
            A.guidance(c, s); return true
        end
        return false
    end
    function A.guidance(c, s)
        if s:variable("ember.region") ~= 0 then return end
        local p = phase(s)
        if p == 1 then a.directive(c, s, "4E4862BB", "SECURITY_INTERCEPTOR_NAV_POINT")
        elseif p == 2 then a.directive(c, s, s:variable("ember.apex.interceptor_boarded") and "4E4862BB" or "3CBFC90B",
            s:variable("ember.apex.interceptor_boarded") and "APEX_DIRECTIVE_REACTOR_CLAMSHELL_GOTO_NAV_POINT" or "SECURITY_INTERCEPTOR_NAV_POINT")
        elseif p == 3 then a.directive(c, s, "26C3C19D", "APEX_DIRECTIVE_REACTOR_CLAMSHELL_GOTO_NAV_POINT")
        elseif p == 4 then a.directive(c, s, "61D2B286", "APEX_DIRECTIVE_REACTOR_COFFIN_TARGET_NAV_POINT")
        elseif p == 5 then a.directive(c, s, "4746660F",
            carry.held(s) and "EMBER_DIRECTIVE_REACTOR_MOTHER_BRAIN_DELIVERY_NAV_POINT" or "APEX_WEAPON_NAV_POINT")
        elseif p == 6 then a.directive(c, s, "40FC40AD", "APEX_DIRECTIVE_REACTOR_RAILS_ESCAPE_NAV_POINT") end
    end
    function A.reset(c, s, name)
        if name == "escape" then
            c:start_timer("ember.apex.hazards", 1)
            set(c, 6)
            -- The weapon is already dead at this checkpoint: restoring it must leave the
            -- beam off and re-arm the escape's own progress triggers.
            beam(c, s, false, true)
            arm_escape(c)
            a.darkness(c, s, true)
        else
            c:cancel_timer(vent_timer(s)); c:cancel_timer("ember.apex.core." .. generation(s))
            c:cancel_timer(surge_audio_timer(s))
            c:cancel_timer("ember.apex.explain." .. generation(s)); c:cancel_timer("ember.apex.setup." .. generation(s))
            a.reset(c, reactor); carry.reset(c, s)
            c:set_variable("ember.apex.generation", generation(s) + 2)
            c:clear_variable("ember.apex.core_ready")
            for _, side in ipairs({"EAST", "WEST", "COFFIN"}) do
                c:clear_variable("ember.apex.dead." .. side); c:clear_variable("ember.apex.healthy." .. side)
            end
            for _, name in ipairs({"EMBER_APEX_REACTOR_CLAMSHELL_EAST_OBJECTIVE", "EMBER_APEX_REACTOR_CLAMSHELL_WEST_OBJECTIVE",
                "EMBER_APEX_REACTOR_COFFIN_OBJECTIVE"}) do a.slot(c, name):reset_objectives{} end
            a.objects(c, {"REACTOR_COFFIN_TARGET_OBJECT"}, false)
            for _, side in ipairs(sides) do
                a.device(c, "CLAMSHELL_TO_COFFIN_" .. side .. "_BRIDGE_DEVICE", true, true)
                a.device(c, "REACTOR_COFFIN_DOOR_" .. side .. "_DEVICE", false, true)
                lane(c, "REACTOR_COFFIN_DOOR_" .. side .. "_DEVICE", "lock")
            end
            lane(c, "MOTHER_BRAIN_DOOR_DEVICE", "lock")
            a.device(c, "MOTHER_BRAIN_DOOR_DEVICE", false, true)
            a.device(c, "COFFIN_BUNKER_DOOR_SOUTH_DEVICE", false, true)
            a.device(c, "REACTOR_SHIELD_DEVICE", false, true)
            -- Detaching the hazard has its own callback: retiring 45 squads already spends
            -- most of this one's native intent budget.
            c:start_timer("ember.apex.hazards", 1)
            set(c, 2)
            -- Publish the reset now; repopulate on the first playable client report
            -- after the wipe handshake. A one-millisecond timer can expire during the fade.
            c:set_variable("ember.apex.restart_pending", true)
        end
        c:clear_variable("ember.r.guidance")
    end
    function A.resume(c, s)
        carry.resume(c, s)
        if phase(s) == 4 and not s:variable("ember.apex.core_ready") then
            c:start_timer("ember.apex.core." .. generation(s), 1)
        end
        a.darkness(c, s, phase(s) >= 3 and phase(s) <= 6)
        -- Streaming back into the area re-attaches the hazard this phase owns.
        if phase(s) == 5 or phase(s) == 6 then c:start_timer("ember.apex.hazards", 1) end
        if phase(s) == 3 or phase(s) == 4 then
            cycle(c, s, "closed", true)
            if phase(s) == 3 and not s:variable("ember.r.cue.41") then c:start_timer("ember.apex.explain." .. generation(s), 9000) end
        end
    end
    function A.interaction(c, s, e)
        if a.matches(c, e, "SECURITY_PLACED_INTERCEPTOR_OBJECT") and e.generation == 1
            and not s:variable("ember.apex.interceptor_boarded") then
            c:set_variable("ember.apex.interceptor_boarded", true)
        end
        carry.interaction(c, s, e)
        A.guidance(c, s)
    end
    function A.object(c, s, e)
        carry.state(c, s, e)
        if e.present and e.alive then
            if a.matches(c, e, "SPECOPS_APEX_RING_LASER_OBJECT")
                and s:variable("ember.apex.beam") == true
                and s:variable("ember.apex.beam_primed") ~= e.generation then
                c:set_variable("ember.apex.beam_primed", e.generation)
                for _, name in ipairs(beam_devices) do unlock(c, name) end
                -- Seeking directly to the resting endpoint left the emitter dark until
                -- its first animated cycle. Traverse the authored drive once on creation
                -- so its effect events run, then leave normal surge timing in charge.
                beam_pose(c, true, true)
                if s:variable("ember.apex.surge") ~= true then beam_pose(c, false, false) end
            end
            if a.matches(c, e, "REACTOR_GETAWAY_SHIP_OBJECT") and phase(s) == 6
                and not s:variable("ember.apex.ship_started") then
                c:set_variable("ember.apex.ship_started", true)
                unlock(c, "REACTOR_GETAWAY_SHIP_DEVICE")
                a.device(c, "REACTOR_GETAWAY_SHIP_DEVICE", false, true)
                a.device(c, "REACTOR_GETAWAY_SHIP_DEVICE", true)
            end
        end
        if e.generation == generation(s) then
            for _, which in ipairs({"EAST", "WEST", "COFFIN"}) do
                local prefix = which == "COFFIN" and "REACTOR_COFFIN" or ("REACTOR_CLAMSHELL_" .. which)
                if a.matches(c, e, prefix .. "_TARGET_OBJECT") and not dead(s, which)
                    and ((which == "COFFIN" and phase(s) == 4) or (which ~= "COFFIN" and phase(s) == 3)) then
                    local seen = "ember.apex.healthy." .. which
                    if e.present and e.alive then
                        if not s:variable(seen) then
                            c:set_variable(seen, true)
                            a.slot(c, prefix .. "_TARGET_DAMAGE"):watch_damage{
                                target = a.slot(c, prefix .. "_TARGET_OBJECT"), revision = generation(s)}
                        end
                    elseif e.present and e.alive == false and s:variable(seen) then
                        destroyed(c, s, which)
                    end
                end
            end
        end
        A.guidance(c, s)
    end
    return A
end
