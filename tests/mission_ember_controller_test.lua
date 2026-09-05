package.path = "scripts/?.lua;" .. package.path
local names = {
    "LANDING_MERCURY_ANCHOR_SQUAD",
    "LANDING_MERCURY_BONUS_ANCHOR_A_SQUAD", "LANDING_MERCURY_SUPPORT_A_SQUAD",
    "LANDING_MERCURY_SUPPORT_B_SQUAD", "LANDING_MERCURY_SUPPORT_C_SQUAD",
    "LANDING_MERCURY_RANGED_A_SQUAD", "LANDING_MERCURY_RANGED_B_SQUAD",
    "LANDING_MERCURY_BONUS_SUPPORT_A_SQUAD", "LANDING_MERCURY_BONUS_SUPPORT_B_SQUAD",
    "LANDING_MERCURY_BONUS_SUPPORT_C_SQUAD", "LANDING_MERCURY_BONUS_SUPPORT_D_SQUAD",
}
local mission = {
    Squad = {}, Slot = {M_DIRECTIVE_SENSOR_80B3C90A = "directive"},
    states = {
        STATE_80B3C09E_0008_0000_80B3C09C = {region_index = 64},
        STATE_80B3C09E_0006_0001_80B3C09A = {region_index = 49},
    },
    DialogueCue = {M_DIALOG_SENSOR_80B3C90A = {CUE_0 = 0, CUE_3 = 3}},
    Directive = {
        FIND_AND_DISABLE_THE_ALMIGHTY_S_WEAPONS_A70DA4A6 = "clear",
        FIND_AND_DISABLE_THE_ALMIGHTY_S_WEAPONS_2700C0C5 = "controls",
    },
}
for _, name in ipairs(names) do
    mission.Squad[name] = name
    mission.Slot[name] = name
end
local devices = {
    "POWERHOUSE_BRIDGE_ARM_MERCURY_DEVICE", "POWERHOUSE_BRIDGE_GEAR_MERCURY_BOTTOM_DEVICE",
    "POWERHOUSE_BRIDGE_GEAR_MERCURY_TOP_DEVICE", "POWERHOUSE_BRIDGE_ARM_SUN_DEVICE",
    "POWERHOUSE_BRIDGE_GEAR_SUN_BOTTOM_DEVICE", "POWERHOUSE_BRIDGE_GEAR_SUN_TOP_DEVICE",
}
for _, name in ipairs(devices) do mission.Slot[name] = name end
mission.Slot.EMBER_POWERHOUSE_BRIDGE_OBJECTIVE = "bridge_objective"
mission.Slot.PF_CINEMATIC_BOOKEND_CINEMATIC = "cinematic"
mission.Slot.M_DIALOG_SENSOR_80B3C90A = "dialogue"
package.preload.missions = function() return {MISSION_EMBER = "test_mission"} end
package.preload.test_mission = function() return mission end
local program = require("mission_ember")
assert(program.initial_state == mission.states.STATE_80B3C09E_0006_0001_80B3C09A,
       "fresh launches must declare the authored cinematic state")
local vars, directives, placements, seeds = {}, {}, 0, 0
local state = {variable = function(_, key) return vars[key] end}
local closed, resets, movies, cues = {}, 0, {}, {}
local context = {sdk = {squad_modes = {replace = "replace"}, device_transitions = {close = "close"}}}
function context:set_phase(value) vars.phase = value end
function context:set_variable(key, value) vars[key] = value end
function context:clear_variable(key) vars[key] = nil end
function context:select_state(entry)
    assert(entry.region_index == 64)
    seeds = seeds + 1
end
function context:squad(name)
    assert(mission.Squad[name])
    return {default_counts = {1}, place = function() placements = placements + 1 end}
end
function context:slot(name)
    if name == "cinematic" then
        return {registry_key = 9, slot_type = 6, slot_index = 0,
            set_cinematic_active = function(_, args) movies[#movies + 1] = args.active end}
    end
    if name == "dialogue" then
        return {play_dialogue_cue = function(_, args) cues[#cues + 1] = args.cue end}
    end
    for _, device in ipairs(devices) do
        if name == device then
            return {transition = function(_, args)
                assert(args.transition == "close" and args.snap == true)
                closed[#closed + 1] = name
            end}
        end
    end
    if name == "bridge_objective" then
        return {reset_objectives = function() resets = resets + 1 end}
    end
    if name == "directive" then
        return {set_directive = function(_, args) directives[#directives + 1] = args.directive end}
    end
    for index, value in ipairs(names) do
        if name == value then return {registry_key = 7, slot_type = 1, slot_index = index} end
    end
    error("unknown slot " .. name)
end
local function region(index)
    program.on_event_client_state_changed(context, state, {current_region_index = index})
end
local function cleared()
    for index = 1, #names do
        program.on_event_squad_state(context, state, {
            registry_key = 7, slot_type = 1, slot_index = index,
            alive_count = 0, previous_alive_count = 1,
        })
    end
end
assert(#closed == 0 and resets == 0 and placements == 0)
program.on_event_client_state_changed(context, state, {region_index = 49})
assert(#movies == 0, "a requested region is not a held cinematic")
region(49); region(49)
assert(#movies == 1 and movies[1] == true and placements == 0)
program.on_event_cinematic_terminated(context, state, {registry_key = 8, slot_type = 6, slot_index = 0})
assert(seeds == 0, "unrelated movie termination cannot advance the mission")
local termination = {registry_key = 9, slot_type = 6, slot_index = 0}
program.on_event_cinematic_terminated(context, state, termination)
program.on_event_cinematic_terminated(context, state, termination)
assert(seeds == 1 and #movies == 2 and movies[2] == false)
region(40)
assert(placements == 0 and #closed == 0)
region(64); region(64)
assert(placements == #names and directives[1] == "clear")
assert(#closed == 6 and resets == 1, "initialize devices only after playable region is held")
program.on_event_client_state_changed(context, state, {teleport_state = 0})
assert(#cues == 0)
region(nil); region(nil)
assert(#cues == 1 and cues[1] == 0, "arrival dialogue follows spawn settlement once")
cleared(); cleared()
assert(#directives == 2 and directives[2] == "controls")
assert(#cues == 2 and cues[2] == 3, "settle deltas must preserve encounter ownership")
package.loaded.mission_ember = nil
program = require("mission_ember")
region(64); cleared()
assert(seeds == 1 and placements == #names and #directives == 2 and #cues == 2)
assert(#closed == 6 and resets == 1, "transit and reload must not reset the bridge")
print("cinematic handoff, held-region initialization, dialogue and reload checks passed")
