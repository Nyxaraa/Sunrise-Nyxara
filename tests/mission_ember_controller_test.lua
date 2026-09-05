package.path = "scripts/?.lua;" .. package.path
local names = {
    "LANDING_MERCURY_ANCHOR_SQUAD", "LANDING_MERCURY_SUPPORT_A_SQUAD",
    "LANDING_MERCURY_SUPPORT_B_SQUAD", "LANDING_MERCURY_SUPPORT_C_SQUAD",
    "LANDING_MERCURY_RANGED_A_SQUAD", "LANDING_MERCURY_RANGED_B_SQUAD",
    "LANDING_MERCURY_BONUS_SUPPORT_A_SQUAD", "LANDING_MERCURY_BONUS_SUPPORT_B_SQUAD",
    "LANDING_MERCURY_BONUS_SUPPORT_C_SQUAD", "LANDING_MERCURY_BONUS_SUPPORT_D_SQUAD",
}
local mission = {
    Squad = {}, Slot = {M_DIRECTIVE_SENSOR_80B3C90A = "directive"},
    states = {STATE_80B3C09E_0008_0000_80B3C09C = {region_index = 64}},
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
package.preload.missions = function() return {MISSION_EMBER = "test_mission"} end
package.preload.test_mission = function() return mission end
local program = require("mission_ember")
assert(program.initial_state == mission.states.STATE_80B3C09E_0008_0000_80B3C09C,
       "fresh launches must declare the opening powerhouse state")
local vars, directives, placements, seeds = {}, {}, 0, 0
local state = {variable = function(_, key) return vars[key] end}
local closed, resets = {}, 0
local context = {sdk = {squad_modes = {replace = "replace"}, device_transitions = {close = "close"}}}
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
program.on_start(context, state)
assert(#closed == 6 and resets == 1 and placements == 0)
region(40)
assert(seeds == 0 and placements == 0)
region(64); region(64)
assert(seeds == 0 and placements == #names and directives[1] == "clear")
region(nil); cleared()
assert(#directives == 1)
region(64)
assert(seeds == 0 and placements == #names)
cleared(); cleared()
assert(#directives == 2 and directives[2] == "controls")
package.loaded.mission_ember = nil
program = require("mission_ember")
region(64); cleared()
assert(seeds == 0 and placements == #names and #directives == 2)
assert(#closed == 6 and resets == 1, "transit and reload must not reset the bridge")
print("controller region ownership, transit, clear and reload checks passed")
