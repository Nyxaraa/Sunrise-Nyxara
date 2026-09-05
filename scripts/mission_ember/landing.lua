-- First combat slice. Wave composition and in-game sequencing still require a live playtest.
local Encounter = require("mission_ember.encounter")

local NAMES = {
    "LANDING_MERCURY_ANCHOR_SQUAD",
    "LANDING_MERCURY_BONUS_ANCHOR_A_SQUAD",
    "LANDING_MERCURY_SUPPORT_A_SQUAD",
    "LANDING_MERCURY_SUPPORT_B_SQUAD",
    "LANDING_MERCURY_SUPPORT_C_SQUAD",
    "LANDING_MERCURY_RANGED_A_SQUAD",
    "LANDING_MERCURY_RANGED_B_SQUAD",
    "LANDING_MERCURY_BONUS_SUPPORT_A_SQUAD",
    "LANDING_MERCURY_BONUS_SUPPORT_B_SQUAD",
    "LANDING_MERCURY_BONUS_SUPPORT_C_SQUAD",
    "LANDING_MERCURY_BONUS_SUPPORT_D_SQUAD",
}

local BRIDGE_DEVICES = {
    "POWERHOUSE_BRIDGE_ARM_MERCURY_DEVICE",
    "POWERHOUSE_BRIDGE_GEAR_MERCURY_BOTTOM_DEVICE",
    "POWERHOUSE_BRIDGE_GEAR_MERCURY_TOP_DEVICE",
    "POWERHOUSE_BRIDGE_ARM_SUN_DEVICE",
    "POWERHOUSE_BRIDGE_GEAR_SUN_BOTTOM_DEVICE",
    "POWERHOUSE_BRIDGE_GEAR_SUN_TOP_DEVICE",
}

local function required(value, name)
    assert(value ~= nil, "mission_ember: missing SDK binding " .. name
           .. "; regenerate the SDK with the scenario-scoped squad linker")
    return value
end

return function(mission)
    local squads = {}
    for index, name in ipairs(NAMES) do
        squads[index] = {
            name = name,
            id = required(mission.Squad[name], "Squad." .. name),
            sensor = required(mission.Slot[name], "Slot." .. name),
        }
    end
    local entry = required(mission.states.STATE_80B3C09E_0008_0000_80B3C09C, "powerhouse state")
    local bridge = {}
    for index, name in ipairs(BRIDGE_DEVICES) do
        bridge[index] = required(mission.Slot[name], "Slot." .. name)
    end
    local bridge_objective = required(mission.Slot.EMBER_POWERHOUSE_BRIDGE_OBJECTIVE,
                                      "bridge objective")
    local directive = required(mission.Slot.M_DIRECTIVE_SENSOR_80B3C90A, "directive sensor")
    local clear = required(mission.Directive.FIND_AND_DISABLE_THE_ALMIGHTY_S_WEAPONS_A70DA4A6,
                           "landing clear directive")
    local controls = required(mission.Directive.FIND_AND_DISABLE_THE_ALMIGHTY_S_WEAPONS_2700C0C5,
                              "bridge controls directive")
    local encounter = Encounter.new("ember.landing", squads, function(context)
        context:slot(directive):set_directive{directive = controls}
        context:slot(mission.Slot.M_DIALOG_SENSOR_80B3C90A):play_dialogue_cue{
            cue = mission.DialogueCue.M_DIALOG_SENSOR_80B3C90A.CUE_3,
        }
        context:set_phase(3)
        -- The bridge must wait for its authored interaction. Combat clear does not open it.
    end)

    local function initialize(context)
        for _, device in ipairs(bridge) do
            context:slot(device):transition{transition = context.sdk.device_transitions.close, snap = true}
        end
        context:slot(bridge_objective):reset_objectives{}
    end

    return {
        initial_state = entry,
        region = entry.region_index,
        enter = function(context, state)
            if encounter:phase(state) ~= 0 then
                return
            end
            initialize(context)
            context:set_phase(2)
            encounter:enter(context, state)
            context:slot(directive):set_directive{directive = clear}
        end,
        client_state = function(context, state, event)
            if event.teleport_state == 0 then context:set_variable("ember.spawned", true) end
            if state:variable("ember.spawned") and not state:variable("ember.guidance")
                and event.region_index == nil and event.current_region_index == nil
                and event.spawn_state == nil and event.teleport_state == nil then
                context:set_variable("ember.guidance", true)
                context:slot(mission.Slot.M_DIALOG_SENSOR_80B3C90A):play_dialogue_cue{
                    cue = mission.DialogueCue.M_DIALOG_SENSOR_80B3C90A.CUE_0,
                }
            end
        end,
        on_squad_state = function(context, state, event)
            encounter:on_squad_state(context, state, event)
        end,
    }
end
