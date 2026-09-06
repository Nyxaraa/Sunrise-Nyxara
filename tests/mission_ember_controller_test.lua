package.path = "scripts/?.lua;" .. package.path
local roster = require("mission_ember.roster")
local order = {"catwalk_entry", "catwalk_mid", "pipe", "mercury", "mercury_bonus", "bridge", "dropships", "passengers", "sun", "helipad"}
local names = {}
local unique = {}
for _, group in ipairs(order) do
    for _, name in ipairs(roster[group]) do
        assert(not unique[name], "duplicate authored squad")
        unique[name] = true
        names[#names + 1] = name
    end
end
assert(#names == 42)
local triggers = {
    "CATWALK_015_PERCENT_PLAYER_TRIGGER", "CATWALK_035_PERCENT_PLAYER_TRIGGER_80B3CC33",
    "CATWALK_100_PERCENT_PLAYER_TRIGGER", "PIPE_CROSSING_040_PERCENT_PLAYER_TRIGGER",
    "LANDING_MERCURY_040_PERCENT_PLAYER_TRIGGER",
    "BRIDGE_025_PERCENT_PLAYER_TRIGGER", "BRIDGE_075_PERCENT_PLAYER_TRIGGER", "BRIDGE_100_PERCENT_PLAYER_TRIGGER",
}
local mission = {
    Squad = {}, Slot = {M_ENGAGEMENT_SENSOR_80B3CB5E = "engagement", M_DIRECTIVE_SENSOR_80B3C90A = "directive", M_MUSIC_SENSOR_80B3C90A = "music"},
    states = {
        STATE_80B3C09E_0008_0000_80B3C09C = {region_index = 64},
        STATE_80B3C09E_0006_0001_80B3C09A = {region_index = 49},
    },
    DialogueCue = {M_DIALOG_SENSOR_80B3C90A = {CUE_0 = 0, CUE_2 = 2, CUE_3 = 3}},
    Directive = {
        FIND_AND_DISABLE_THE_ALMIGHTY_S_WEAPONS_4EA80049 = "follow",
        FIND_AND_DISABLE_THE_ALMIGHTY_S_WEAPONS_A70DA4A6 = "clear",
        FIND_AND_DISABLE_THE_ALMIGHTY_S_WEAPONS_2700C0C5 = "controls",
        FIND_AND_DISABLE_THE_ALMIGHTY_S_WEAPONS_AFBF882A = "cross",
    },
}
for _, name in ipairs(names) do
    mission.Squad[name] = name
    mission.Slot[name] = name
end
for _, name in ipairs(triggers) do mission.Slot[name] = name end
local markerNames = {"POWERHOUSE_DIRECTIVE_LANDING_MERCURY_CLEAR_NAV_POINT",
    "POWERHOUSE_DIRECTIVE_LANDING_MERCURY_INTERACT_NAV_POINT", "HELIPAD_SUN_SQUAD_FORMATION_NAV_POINT"}
for index, name in ipairs(markerNames) do
    mission.Slot[name] = {registry_key = 77, slot_type = 47, slot_index = index}
end
local navpoints = {}
local flights, deliveries, retiredShips = {}, {}, 0
local exitActions = {}
local darkness = {}
mission.Slot.HARD_WIPE_GLOBALS = "darkness"
local timers, timerStarts, now = {}, 0, 0
for index, letter in ipairs({"A", "B", "C", "D"}) do
    local prefix = "BRIDGE_CROSSING_DROPSHIP_" .. letter
    for _, suffix in ipairs({"_SQUAD_HARVESTER", "_ENTRY_SEQUENCE", "_EXIT_SEQUENCE"}) do
        mission.Slot[prefix .. suffix] = {registry_key = 7, slot_type = suffix == "_SQUAD_HARVESTER" and 2 or 58,
            slot_index = index, play_actor_path = function(_, args) flights[#flights + 1] = args end,
            deliver_squads = function(_, args)
                assert(index <= 2, "off-bridge ships cannot receive troops")
                assert(args.generation % 2 == 1 and args.revision == 1 and #args.squads == 2)
                assert(args.squads[1].slot_index == 33 + index and args.squads[2].slot_index == 35 + index,
                    "each bridge ship carries its support and melee squad")
                deliveries[#deliveries + 1] = args
            end,
            play_actor_action = function(_, args)
                assert(args.generation % 2 == 1 and args.revision == 3,
                    "exit action must preserve the ship and advance only the command revision")
                assert(args.group == 0x811C9DC5 and args.action == 0x7B0D3643)
                exitActions[#exitActions + 1] = args
            end,
            retire_actor = function(_, args)
                assert(args.generation % 2 == 0, "retirement must advance the native lifecycle"); retiredShips = retiredShips + 1
            end}
    end
end
local assignments = {}
for _, area in ipairs({"CATWALK", "PIPE_CROSSING", "LANDING_MERCURY", "BRIDGE", "LANDING_SUN", "HELIPAD_SUN"}) do
    local key = "EMBER_POWERHOUSE_" .. area .. "_OBJECTIVE"
    mission.Slot[key] = {objective_name = key}
end
local armed = {}
local devices = {
    "POWERHOUSE_BRIDGE_ARM_MERCURY_DEVICE", "POWERHOUSE_BRIDGE_GEAR_MERCURY_BOTTOM_DEVICE",
    "POWERHOUSE_BRIDGE_GEAR_MERCURY_TOP_DEVICE", "POWERHOUSE_BRIDGE_ARM_SUN_DEVICE",
    "POWERHOUSE_BRIDGE_GEAR_SUN_BOTTOM_DEVICE", "POWERHOUSE_BRIDGE_GEAR_SUN_TOP_DEVICE",
}
for _, name in ipairs(devices) do mission.Slot[name] = name end
mission.Slot.EMBER_POWERHOUSE_BRIDGE_OBJECTIVE = "bridge_objective"
mission.Slot.LANDING_MERCURY_CONSOLE_BUTTON_GHOST_LINK = "ghost"
mission.Slot.LANDING_MERCURY_DOOR_BUTTON_OBJECT = "lever"
mission.Slot.POWERHOUSE_LANDING_MERCURY_DOOR_DEVICE = "door"
mission.Slot.POWERHOUSE_LANDING_MERCURY_DOOR_LEVER_DEVICE = "lever_device"
mission.Slot.PF_CINEMATIC_BOOKEND_CINEMATIC = "cinematic"
mission.Slot.M_DIALOG_SENSOR_80B3C90A = "dialogue"
package.preload.missions = function() return {MISSION_EMBER = "test_mission"} end
package.preload.test_mission = function() return mission end
-- Match the production sandbox: unordered iteration is deliberately unavailable.
pairs = nil
next = nil
-- This fixture deliberately supplies only the 42 Powerhouse squads. Later controllers
-- are exercised separately against the complete generated SDK.
package.preload["mission_ember.routes"] = function() return function() return {
    client = function() end, dispatch = function() end, squad = function() end, terminated = function() end,
} end end
local program = require("mission_ember")
assert(program.initial_state == mission.states.STATE_80B3C09E_0006_0001_80B3C09A,
       "fresh launches must declare the authored cinematic state")
local vars, directives, placements, seeds = {}, {}, 0, 0
local retirements = 0
local variable_count, peak_variables = 0, 0
local state = {variable = function(_, key) return vars[key] end}
local closed, resets, movies, cues = {}, 0, {}, {}
local ghost, door, extended = {}, {}, 0
local leverAnimation = {}
local context = {sdk = {squad_modes = {replace = "replace", reserve = "reserve"}, device_transitions = {close = "close", open = "open"}}}
function context:start_timer(name, delay)
    assert(delay == (name:find("depart", 1, true) and 4000 or 6000),
           "unload adds three seconds to the prior three-second settle; departure holds four seconds")
    timers[name] = now + delay
    timerStarts = timerStarts + 1
end
local function tick(time)
    now = time
    for _, stage in ipairs({"unload", "depart"}) do
        for index = 1, 4 do
            local name = "ember.transport." .. stage .. "." .. index
            if timers[name] and now >= timers[name] then
                timers[name] = nil
                program.on_event_timer_elapsed(context, state, {timer_name = name})
            end
        end
    end
end
function context:set_phase(value) vars.phase = value end
function context:set_variable(key, value)
    if vars[key] == nil then variable_count = variable_count + 1 end
    peak_variables = math.max(peak_variables, variable_count)
    assert(variable_count <= 512, "mission variable capacity exceeded")
    vars[key] = value
end
function context:clear_variable(key)
    if vars[key] ~= nil then variable_count = variable_count - 1 end
    vars[key] = nil
end
function context:select_state(entry)
    assert(entry.region_index == 64)
    seeds = seeds + 1
end
function context:squad(name)
    assert(mission.Squad[name])
    return {default_counts = {1},
        counts = function() return {set = function(self, index, value) assert(index == 1 and value == 0); self.zero = true end} end,
        place = function(_, args)
            if name:find("DROPSHIP_[ABCD]_SQUAD$") then assert(args.counts.zero) end
            if name:find("DROPSHIP_A_SUPPORT") or name:find("DROPSHIP_A_MELEE") then
                assert(args.mode == "reserve", "cargo cannot use ordinary ground placement")
            else assert(args.mode == "replace") end
            if args.counts and args.counts.zero and not name:find("DROPSHIP_[ABCD]_SQUAD$") then
                retirements = retirements + 1
            else placements = placements + 1 end
        end}
end
function context:slot(name)
    if name == "music" then return {set_music_section = function(_, args) assert(args.section >= 0 and args.section <= 28) end} end
    if name == "darkness" then return {set_darkness_zone = function(_, args) darkness[#darkness + 1] = args.enabled end} end
    if type(name) == "table" then return name end
    for index, trigger in ipairs(triggers) do
        if name == trigger then
            return {registry_key = 10, slot_type = 31, slot_index = index,
                fire_trigger = function() armed[index] = (armed[index] or 0) + 1 end}
        end
    end
    if name == "cinematic" then
        return {registry_key = 9, slot_type = 6, slot_index = 0,
            set_cinematic_active = function(_, args) movies[#movies + 1] = args.active end}
    end
    if name == "dialogue" then
        return {play_dialogue_cue = function(_, args) cues[#cues + 1] = args.cue end}
    end
    for index, device in ipairs(devices) do
        if name == device then
            return {registry_key = 13, slot_type = 23, slot_index = index, transition = function(_, args)
                if args.snap then
                    assert(args.transition == "open")
                    closed[#closed + 1] = name
                else
                    assert(args.transition == "close")
                    extended = extended + 1
                end
            end}
        end
    end
    if name == "lever" then
        return {registry_key = 12, slot_type = 4, slot_index = 19,
            set_interactable_object = function(_, args) assert(args.generation == 1) end}
    end
    if name == "ghost" then
        return {registry_key = 12, slot_type = 65, slot_index = 60,
            set_ghost_link = function(_, args) ghost[#ghost + 1] = args end}
    end
    if name == "lever_device" then
        return {transition = function(_, args) leverAnimation[#leverAnimation+1] = args end}
    end
    if name == "door" then
        return {transition = function(_, args) door[#door + 1] = args end}
    end
    if name == "bridge_objective" then
        return {objective_name = "bridge_objective", reset_objectives = function() resets = resets + 1 end}
    end
    if name == "engagement" then
        return {set_engagement_state = function(_, args) assert(args.flags == 0 and args.revision == 1) end}
    end
    if name == "directive" then
        return {set_directive = function(_, args) if directives[#directives] ~= args.directive then directives[#directives + 1] = args.directive end; navpoints[#navpoints + 1] = args.navpoint or false end}
    end
    for index, value in ipairs(names) do
        if name == value then return {registry_key = 7, slot_type = 1, slot_index = index,
            assign_combat_objective = function(_, args)
                assert((args.task_group == -1 or (name:find("DROPSHIP_[ABCD]_SQUAD$") and args.task_group == 1)) and args.revision == 1)
                assert(args.reserved == (name:find("DROPSHIP_A_SUPPORT") ~= nil or name:find("DROPSHIP_A_MELEE") ~= nil),
                       "objective updates must preserve the passenger reservation")
                assignments[name] = args.objective
            end} end
    end
    error("unknown slot " .. name)
end
local function region(index)
    program.on_event_client_state_changed(context, state, {current_region_index = index})
end
local function cleared(first, last)
    for index = first or 12, last or 22 do
        program.on_event_squad_state(context, state, {
            registry_key = 7, slot_type = 1, slot_index = index,
            alive_count = 0, previous_alive_count = 1, removal_flag = true, slot_counts = {1},
        })
    end
end
assert(#closed == 0 and resets == 0 and placements == 0)
program.on_event_client_state_changed(context, state, {region_index = 49})
assert(#movies == 0, "a requested region is not a held cinematic")
region(64)
assert(placements == 0 and #closed == 0 and #cues == 0,
       "playable-region reports cannot start the landing before cinematic completion")
region(49); region(49)
assert(#movies == 1 and movies[1] == true and placements == 0)
program.on_event_cinematic_terminated(context, state, {registry_key = 8, slot_type = 6, slot_index = 0})
assert(seeds == 0, "unrelated movie termination cannot advance the mission")
local termination = {registry_key = 9, slot_type = 6, slot_index = 0}
-- A skip request must match the current movie, and late completion must not spawn twice.
program.on_event_cinematic_skip_requested(context, state, {registry_key = 8, slot_type = 6, slot_index = 0})
assert(seeds == 0, "an unrelated skip cannot advance the mission")
program.on_event_cinematic_skip_requested(context, state, termination)
program.on_event_cinematic_skip_requested(context, state, termination)
program.on_event_cinematic_terminated(context, state, termination)
program.on_event_cinematic_terminated(context, state, termination)
assert(seeds == 1 and #movies == 2 and movies[2] == false)
region(40)
assert(placements == 0 and #closed == 0)
region(64); region(64)
assert(placements == 5 and directives[1] == "follow")
assert(#darkness == 1 and darkness[1] == false, "arrival must leave darkness disabled")
for index = 1, #triggers do assert(armed[index] == (index < 6 and 1 or nil)) end
assert(#closed == 6 and resets == 1, "initialize devices only after playable region is held")
program.on_event_client_state_changed(context, state, {teleport_state = 0})
assert(#cues == 0)
region(nil); region(nil)
assert(#cues == 1 and cues[1] == 0, "arrival dialogue follows spawn settlement once")
-- Initial zero and unrelated trigger reports must not complete or populate later fights.
for index = 12, 22 do
    program.on_event_squad_state(context, state, {registry_key = 7, slot_type = 1,
        slot_index = index, alive_count = 0, previous_alive_count = 0})
end
assert(#directives == 1)
local function trigger(index, registry)
    program.on_event_player_trigger(context, state, {
        registry_key = registry or 10, slot_type = 31, slot_index = index,
    })
end
trigger(1, 99); assert(placements == 5)
trigger(1); trigger(1); assert(placements == 9)
trigger(2); assert(placements == 11)
trigger(3); assert(placements == 19 and directives[2] == "clear")
for _, name in ipairs({"BRIDGE_VIGNETTE_SUPPORT_A_SQUAD", "BRIDGE_VIGNETTE_SUPPORT_B_SQUAD"}) do
    assert(assignments[name].objective_name == "bridge_objective",
           "near-side squads retain their authored bridge AI objective")
end
trigger(4); trigger(4); assert(#cues == 2 and cues[2] == 2)
assert(#ghost == 1 and ghost[1].generation == 1 and not ghost[1].enabled)
assert(#door == 1 and door[1].transition == "close" and door[1].snap)
assert(#leverAnimation == 1 and leverAnimation[1].transition == "close" and leverAnimation[1].snap)
trigger(5); trigger(5)
trigger(8); trigger(6); trigger(7)
assert(placements == 19, "overlapping bridge receipts must not spawn waves before the scan")
assert(#door == 1, "proximity cannot open the lever door or spawn its group")
local function console_complete(generation, progress, active, registry)
    program.on_event_ghost_link_state(context, state, {registry_key = registry or 12,
        slot_type = 65, slot_index = 60, generation = generation, progress = progress, active = active})
end
console_complete(2, 1, false)
assert(extended == 0, "console cannot complete before combat")
cleared(12,17)
assert(#directives == 2, "near-side bridge squads must also clear before Ghost becomes usable")
cleared(18,18); assert(#directives == 2)
cleared(19,19); cleared()
assert(#directives == 3 and directives[3] == "controls" and retirements == 8)
assert(#cues == 3 and cues[3] == 3)
assert(#closed == 6 and extended == 0, "combat clear cannot move the bridge")
assert(#ghost == 2 and ghost[2].generation == 2 and ghost[2].enabled)
assert(not vars["ember.mercury_bonus.phase"], "Crimson Shadow cannot block Ghost interaction")
local function use_lever(registry, slot_type, slot_index)
    program.on_event_object_interacted(context, state, {
        registry_key = registry or 12, slot_type = slot_type or 4, slot_index = slot_index or 19})
end
use_lever(99); use_lever(12,65); use_lever(12,4,20)
assert(#door == 1 and not vars["ember.mercury_bonus.phase"], "unrelated objects cannot use the lever")
use_lever(); use_lever()
assert(#door == 2 and door[2].transition == "open" and not door[2].snap)
assert(#leverAnimation == 2 and leverAnimation[2].transition == "open" and not leverAnimation[2].snap,
       "lever use must animate the matching authored device once")
assert(vars["ember.mercury_bonus.phase"] == 1, "lever use starts the optional group exactly once")
cleared(20,24)
assert(#directives == 3 and #ghost == 2, "bonus completion cannot replace the bridge instruction")
console_complete(1, 1, false); console_complete(2, 1, false, 99)
console_complete(2, .5, false); console_complete(2, 1, true)
assert(extended == 0, "only matching completed native interaction can extend the bridge")
assert(#darkness == 1 and not darkness[1], "combat clear and incomplete scans cannot enable darkness")
-- Prepare reserved cargo and empty parents before Ghost is usable, without starting ships.
assert(placements == 32 and #flights == 0 and #deliveries == 0)
program.on_event_squad_state(context, state, {registry_key = 7, slot_type = 1, slot_index = 30,
    objective_revision = 1, task_costs = {2040, 10, 20}})
assert(vars["ember.transport.ai"] == 2, "Harvester chooses its native minimum-cost task")
assert(placements == 32 and #flights == 0 and #deliveries == 0,
    "ship objective assignment must preserve empty requests and deferred flight")
console_complete(2, 1, false); console_complete(2, 1, false)
assert(extended == 6 and directives[4] == "cross" and vars.phase == 4)
assert(#darkness == 2 and darkness[2], "console completion enables darkness once with bridge extension")
assert(placements == 37 and #flights == 4 and #deliveries == 0,
       "bridge activation starts ships and all five far-side hangar squads exactly once")
for _, group in ipairs({"sun", "helipad"}) do
    assert(vars["ember." .. group .. ".phase"] == 1, "hangar waves cannot wait for the crossing")
end
-- Crossing stages the seven deck defenders; Sun and helipad remain one-shot.
trigger(6); assert(placements == 42)
trigger(6); assert(placements == 42)
trigger(7); assert(placements == 42)
trigger(8); assert(placements == 42)
trigger(6); trigger(7); assert(placements == 42)
package.loaded.mission_ember = nil
program = require("mission_ember")
region(64); cleared(); trigger(8)
assert(#darkness == 2 and darkness[2], "crossings and reload cannot restart darkness")
assert(seeds == 1 and placements == 42 and #directives == 4 and #cues == 3)
for index = 1, #triggers do assert(armed[index] == 1) end
assert(#closed == 6 and resets == 1, "transit and reload must not reset the bridge")
local function flight(index, revision, status, generation, dead, delivery_revision, delivery_state)
    program.on_event_actor_path_state(context, state, {registry_key = 7, slot_type = 2, slot_index = index,
        generation = generation or 1, revision = revision, path_state = status, dead = dead or false,
        delivery_revision = delivery_revision, delivery_state = delivery_state})
end
flight(1, 1, 0); flight(1, 1, 1, 2, false, 1, 0)
assert(#flights == 4 and #deliveries == 0, "initial and stale member reports cannot begin unloading")
for index = 1, 3 do flight(index, 1, 1, 1, false, 0, 0) end
assert(#deliveries == 0 and timerStarts == 3)
flight(1, 1, 1, 1, false, 0, 0)
assert(timerStarts == 3, "duplicate arrival cannot restart the unload delay")
tick(5999)
assert(#deliveries == 0, "hold cargo for six seconds after arrival")
package.loaded.mission_ember = nil
program = require("mission_ember")
region(56)
assert(darkness[#darkness] == false, "leaving the encounter must release respawn restrictions")
tick(6000)
assert(#deliveries == 2 and #flights == 4 and timerStarts == 4, "A/B unload; empty C begins its departure hold")
program.on_event_timer_elapsed(context, state, {timer_name = "ember.transport.unload.1"})
assert(#deliveries == 2, "duplicate timer receipt cannot deliver twice")
flight(4, 1, 0, 1, false, 0, 0)
assert(timerStarts == 4)
flight(4, 1, 1, 1, false, 0, 0)
tick(9999); assert(#flights == 4)
tick(10000); assert(#flights == 5, "C leaves after its separate four-second departure hold")
tick(11999); assert(#flights == 5)
tick(12000); assert(#deliveries == 2 and #flights == 5 and timerStarts == 6)
for index = 1, 2 do flight(index, 1, 1, 1, false, 1, 1) end
assert(placements == 42, "manifests must not add ground spawn requests")
flight(1, 1, 1, 1, false, 1, 0)
assert(#flights == 5)
program.on_event_squad_state(context, state, {registry_key = 7, slot_type = 1, slot_index = 34,
    alive_count = 1, previous_alive_count = 0})
assert(#flights == 5, "A waits for both of its passenger squads")
program.on_event_squad_state(context, state, {registry_key = 7, slot_type = 1, slot_index = 36,
    alive_count = 1, previous_alive_count = 0})
assert(#flights == 5 and timerStarts == 7, "A begins its hold after both squads and detach completed")
for _, index in ipairs({35,37}) do
    program.on_event_squad_state(context, state, {registry_key = 7, slot_type = 1, slot_index = index,
        alive_count = 1, previous_alive_count = 0})
end
assert(#flights == 5 and timerStarts == 7, "B still waits for native detach completion")
flight(2, 1, 1, 1, false, 1, 0)
assert(#flights == 5 and timerStarts == 8)
-- Reload and duplicate receipts cannot restart or bypass a hold.
package.loaded.mission_ember = nil
program = require("mission_ember")
flight(2, 1, 1, 1, false, 1, 0)
assert(timerStarts == 8)
tick(15999); assert(#flights == 5)
tick(16000)
assert(#flights == 8 and flights[5].revision == 2 and flights[5].generation == 1)
for index = 1,4 do
    program.on_event_timer_elapsed(context,state,{timer_name="ember.transport.depart."..index})
end
assert(#flights == 8 and timerStarts == 8, "duplicate departure timers cannot repeat paths")
for index = 1, 4 do flight(index, 1, 1, 1, false, 1, 0); flight(index, 2, 0) end
assert(retiredShips == 0, "exit requests cannot retire a ship before it finishes flying")
for index = 1, 4 do flight(index, 2, 1); flight(index, 2, 1) end
assert(retiredShips == 0 and #exitActions == 4, "path end starts one native exit action per ship")
-- Stale path receipts, module reload and running-action receipts cannot retire or restart ships.
package.loaded.mission_ember = nil
program = require("mission_ember")
for index = 1,4 do flight(index, 2, 1); flight(index, 3, 0) end
assert(retiredShips == 0 and #exitActions == 4)
for index = 1,4 do flight(index, 3, 1); flight(index, 3, 1) end
assert(retiredShips == 4 and #exitActions == 4, "native exit completion retires each ship once")
console_complete(2, 1, false)
assert(placements == 42 and #flights == 8 and #deliveries == 2,
       "duplicate path and scan receipts cannot respawn ships or passengers")
region(64); cleared()
assert(#ghost == 2 and extended == 6)
print("cinematic handoff, 42-squad route, skipped volumes, exact triggers, dialogue and reload checks passed")

for _, group in ipairs({"catwalk_entry", "catwalk_mid", "pipe", "mercury", "mercury_bonus", "bridge", "passengers", "sun", "helipad"}) do
    for _, name in ipairs(roster[group]) do assert(assignments[name], "missing objective: " .. name) end
end
for _, name in ipairs(roster.dropships) do
    assert(assignments[name], "each ship parent needs bridge objective membership")
end

print("full-route peak durable variables: " .. peak_variables .. "/512")

-- Completed native requests are zeroed once. Returning from the refinery, including a
-- module reload and late trigger receipts, cannot replay any opening side effect.
cleared(25,29); cleared(34,36); cleared(38,39)
assert(darkness[#darkness] == true, "one remaining passenger must hold darkness")
cleared(37,37)
assert(darkness[#darkness] == false, "the final bridge passenger clears darkness without a load zone")
assert(vars["ember.helipad.phase"] == 1, "later helipad combat cannot hold bridge darkness")
program.on_event_squad_state(context,state,{registry_key=7,slot_type=1,slot_index=40,
    alive_count=1,previous_alive_count=0})
assert(darkness[#darkness] == false, "live helipad enemies cannot reactivate the cleared bridge")
cleared(40,42)
assert(vars["ember.catwalk_entry.phase"] == 1, "earlier encounters are outside bridge darkness completion")
cleared(1,11)
assert(retirements == 38)
local darknessCount = #darkness
local objectiveCount, cueCount, doorCount = #directives, #cues, #door
local movieCount, flightCount = #movies, #flights
-- A completed checkpoint may never have crossed these narrow early volumes.
context:clear_variable("ember.route.PIPE_CROSSING_040_PERCENT_PLAYER_TRIGGER")
context:clear_variable("ember.route.LANDING_MERCURY_040_PERCENT_PLAYER_TRIGGER")
region(56)
package.loaded.mission_ember = nil
program = require("mission_ember")
region(49); region(64)
for index = #triggers, 1, -1 do trigger(index) end
cleared(1,42)
console_complete(2,1,false)
use_lever()
assert(#darkness == darknessCount, "completed-area return cannot restore restrictions")
assert(retirements == 38 and placements == 42 and retiredShips == 4)
assert(#directives == objectiveCount and #cues == cueCount and #door == doorCount and #leverAnimation == 2)
assert(#movies == movieCount and #flights == flightCount and #closed == 6 and extended == 6)
print("completed-area return preserves objectives, devices, cinematic and zero native spawn requests")

-- Guidance follows route progress and never leaves a completed approach pointing backward.
local landing = require("mission_ember.landing")(mission)
local function guidance() landing.update_guidance(context,state); return navpoints[#navpoints] end
vars["ember.bridge.extended"]=false;vars["ember.console.enabled"]=false
vars["ember.route.progress"]=0;vars["ember.navpoint"]=nil
assert(guidance().slot_index==1, "cleared approach points toward Mercury")
vars["ember.landing.phase"]=1
assert(guidance()==false, "combat removes navigation")
vars["ember.landing.phase"]=2;vars["ember.console.enabled"]=true
assert(guidance().slot_index==2, "cleared landing points to Ghost")
local markers=#navpoints;guidance();assert(#navpoints==markers,"unchanged target is idempotent")
vars["ember.bridge.extended"]=true;vars["ember.sun.phase"]=1
assert(guidance()==false)
vars["ember.sun.phase"]=2
assert(guidance().slot_index==3,"completed bridge points forward to the helipad")
vars["ember.route.progress"]=8;assert(guidance()==false,"reaching helipad retires the approach marker")
-- Reset the bridge and scan again without replaying the completed approach or lever.
function context:cancel_timer(name) timers[name]=nil end
local beforeMovies,beforeLever=#movies,#leverAnimation
local beforeFlights=#flights
landing.reset_checkpoint(context,state)
assert(not vars["ember.bridge.extended"] and not vars["ember.darkness.enabled"])
assert(vars["ember.route.progress"]==5 and vars["ember.console.generation"]==4)
assert(vars["ember.transport.generation"]==3 and not vars["ember.transport.started"])
assert(vars["ember.catwalk_entry.phase"]==2 and vars["ember.landing.phase"]==2)
assert(vars["ember.lever.used"] and #leverAnimation==beforeLever and #movies==beforeMovies)
assert(ghost[#ghost].enabled and ghost[#ghost].generation==4)
assert(#closed==12 and #flights==beforeFlights)
console_complete(2,1,false);assert(#flights==beforeFlights,"prior Ghost completion cannot rescan")
console_complete(4,1,false);assert(#flights==beforeFlights+4)
assert(flights[#flights].generation==3 and darkness[#darkness])
local deliveryCount=#deliveries
program.on_event_actor_path_state(context,state,{registry_key=7,slot_type=2,slot_index=1,
    generation=1,revision=1,path_state=1})
program.on_event_timer_elapsed(context,state,{timer_name="ember.transport.unload.1"})
assert(#deliveries==deliveryCount,"old flight/timer cannot unload next attempt")
program.on_event_actor_path_state(context,state,{registry_key=7,slot_type=2,slot_index=1,
    generation=3,revision=1,path_state=1})
assert(timers["ember.transport.unload.1.3"],"next flight has its own unload timer")
program.on_event_timer_elapsed(context,state,{timer_name="ember.transport.unload.1"})
assert(#deliveries==deliveryCount)
program.on_event_timer_elapsed(context,state,{timer_name="ember.transport.unload.1.3"})
assert(#deliveries==deliveryCount+1 and deliveries[#deliveries].generation==3)
print("navpoint combat gating, checkpoint rescan and second-flight stale-event checks passed")
