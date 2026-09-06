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
    local function phase(s) return s:variable("ember.apex.phase") or 0 end
    local function set(c, p) c:set_variable("ember.apex.phase", p) end
    local function generation(s) return s:variable("ember.apex.generation") or 1 end
    local function dead(s, target) return s:variable("ember.apex.dead." .. target) == true end
    local function vent_timer(s) return "ember.apex.vents." .. generation(s) end
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
    local function beam(c, active, snap)
        -- Both devices address the placed ring/laser objects and their native VFX graphs.
        a.device(c, "SPECOPS_APEX_RING_LASER_DEVICE", active, snap)
        a.device(c, "SPECOPS_APEX_RING_RING_DEVICE", active, snap)
        -- FX power is independent of mechanical position; pulse both authored graphs.
        lane(c, "SPECOPS_APEX_RING_LASER_DEVICE", active and "power_on" or "power_off", snap)
        lane(c, "SPECOPS_APEX_RING_RING_DEVICE", active and "power_on" or "power_off", snap)
    end
    local function hazards(c, s, enabled)
        a.effect(c, s, "REACTOR_MOTHER_BRAIN_HOT_PIPES_THERMAL_HOP_ON",
            "REACTOR_MOTHER_BRAIN_HOT_PIPES_OBJECT_FILTER_80B3C09F",
            {players = true, inside_any = {
                a.slot(c, "REACTOR_MOTHER_BRAIN_HOT_PIPES_02_TRIGGER_VOLUME"),
                a.slot(c, "REACTOR_MOTHER_BRAIN_HOT_PIPES_03_TRIGGER_VOLUME"),
                a.slot(c, "SLOT_0005_80B3C09F"), a.slot(c, "SLOT_0006_80B3C09F"),
                a.slot(c, "SLOT_0008_80B3C09F")}}, enabled)
        a.effect(c, s, "AOD_REACTOR_RAIL_TOP_HOP_ON", "AOD_REACTOR_RAIL_TOP_OBJECT_FILTER",
            {players = true, inside = a.slot(c, "SLOT_019E")}, enabled)
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
            beam(c, true)
            if phase(s) == 3 then
                for _, side in ipairs(sides) do
                    if not dead(s, side) then a.slot(c, "REACTOR_CLAMSHELL_" .. side .. "_ALARM_SEQUENCE"):play_sequence{} end
                end
            else a.slot(c, "REACTOR_COFFIN_ALARM_SEQUENCE"):play_sequence{} end
        else
            if step == "closed" then beam(c, false, snap) end
            if phase(s) == 3 then
                for _, side in ipairs(sides) do if not dead(s, side) then doors(c, side, step == "open", snap) end end
            elseif phase(s) == 4 then
                coffin_doors(c, step == "open", snap)
                if step == "open" then a.cue(c, s, 46) end
            end
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
        beam(c, false, true)
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
            a.device(c, "REACTOR_GETAWAY_SHIP_DEVICE", true)
            a.objects(c, {"REACTOR_GETAWAY_SHIP_OBJECT", "SUNBURN_DAMAGE_OBJECT"}, true)
            a.device(c, "SPECOPS_APEX_RING_LASER_DEVICE", true)
            a.device(c, "SPECOPS_APEX_RING_RING_DEVICE", true)
            a.scene(c, "MOTHER_BRAIN_HOLE_EXPLOSION_SCENE")
            a.scene(c, "EMBER_APEX_EXPLOSION_SEQUENCE_PREFAB_TRIGGERED_EXPLOSIONS_SCENE")
            for _, name in ipairs(escape_triggers) do a.slot(c, name):fire_trigger{} end
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
        a.objects(c, {"SPECOPS_APEX_RING_LASER_OBJECT",
            "SPECOPS_APEX_RING_CORE_OBJECT", "SPECOPS_APEX_RING_RING_OBJECT"}, true)
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
                hazards(c, s, false)
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
            coffin_doors(c, true)
            beam(c, true)
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
        if e.timer_name == "ember.apex.hazards" then
            if phase(s) == 6 and s:variable("ember.region") == 0 then hazards(c, s, true) end
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
        if p == 1 then a.directive(c, s, "4E4862BB", "SECURITY_INTERCEPTOR_NAV_POINT", a.combat(s, access))
        elseif p == 2 then a.directive(c, s, s:variable("ember.apex.interceptor_boarded") and "4E4862BB" or "3CBFC90B",
            s:variable("ember.apex.interceptor_boarded") and "APEX_DIRECTIVE_REACTOR_CLAMSHELL_GOTO_NAV_POINT" or "SECURITY_INTERCEPTOR_NAV_POINT", a.combat(s, access))
        elseif p == 3 then a.directive(c, s, "26C3C19D", "APEX_DIRECTIVE_REACTOR_CLAMSHELL_GOTO_NAV_POINT", a.combat(s, reactor))
        elseif p == 4 then a.directive(c, s, "61D2B286", "APEX_DIRECTIVE_REACTOR_COFFIN_TARGET_NAV_POINT", a.combat(s, reactor))
        elseif p == 5 then a.directive(c, s, "4746660F",
            carry.held(s) and "EMBER_DIRECTIVE_REACTOR_MOTHER_BRAIN_DELIVERY_NAV_POINT" or "APEX_WEAPON_NAV_POINT", a.combat(s, reactor))
        elseif p == 6 then a.directive(c, s, "40FC40AD", "APEX_DIRECTIVE_REACTOR_RAILS_ESCAPE_NAV_POINT", false) end
    end
    function A.reset(c, s, name)
        if name == "escape" then
            c:start_timer("ember.apex.hazards", 1)
            set(c, 6)
            for _, t in ipairs(escape_triggers) do a.slot(c, t):fire_trigger{} end
            a.darkness(c, s, true)
        else
            c:cancel_timer(vent_timer(s)); c:cancel_timer("ember.apex.core." .. generation(s))
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
