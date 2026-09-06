-- First combat slice. Wave composition and in-game sequencing still require a live playtest.
local Encounter = require("mission_ember.encounter")

local ROSTER = require("mission_ember.roster")
local GROUP_ORDER = {"catwalk_entry", "catwalk_mid", "pipe", "mercury",
                     "bridge", "sun", "helipad", "passengers", "mercury_bonus"}
-- These are authored volume crossings. Timing/group choices are reconstructed route policy.
local ROUTE = {
    {trigger = "CATWALK_015_PERCENT_PLAYER_TRIGGER", groups = {"catwalk_mid"}},
    {trigger = "CATWALK_035_PERCENT_PLAYER_TRIGGER_80B3CC33", groups = {"pipe"}},
    {trigger = "CATWALK_100_PERCENT_PLAYER_TRIGGER", groups = {"mercury"}},
    {trigger = "PIPE_CROSSING_040_PERCENT_PLAYER_TRIGGER", groups = {"mercury"}, cue = 2},
    {trigger = "LANDING_MERCURY_040_PERCENT_PLAYER_TRIGGER", groups = {"mercury"}},
    {trigger = "BRIDGE_025_PERCENT_PLAYER_TRIGGER", groups = {"bridge"}},
    {trigger = "BRIDGE_075_PERCENT_PLAYER_TRIGGER", groups = {"sun"}},
    {trigger = "BRIDGE_100_PERCENT_PLAYER_TRIGGER", groups = {"helipad"}},
}

-- Counts are read from each objective's authored task-group array (source +136).
local OBJECTIVES = {
    catwalk_entry = {"EMBER_POWERHOUSE_CATWALK_OBJECTIVE", 17}, -- 80B3CA03
    catwalk_mid = {"EMBER_POWERHOUSE_CATWALK_OBJECTIVE", 17},
    pipe = {"EMBER_POWERHOUSE_PIPE_CROSSING_OBJECTIVE", 3}, -- 80B3DCC7
    mercury = {"EMBER_POWERHOUSE_LANDING_MERCURY_OBJECTIVE", 11}, -- 80B3DCC1
    mercury_bonus = {"EMBER_POWERHOUSE_LANDING_MERCURY_OBJECTIVE", 11},
    bridge = {"EMBER_POWERHOUSE_BRIDGE_OBJECTIVE", 14}, -- 80B3C8D6
    passengers = {"EMBER_POWERHOUSE_BRIDGE_OBJECTIVE", 14},
    sun = {"EMBER_POWERHOUSE_LANDING_SUN_OBJECTIVE", 4}, -- 80B3DCC4
    helipad = {"EMBER_POWERHOUSE_HELIPAD_SUN_OBJECTIVE", 4}, -- 80B3DDB2
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
    local music = require("mission_ember.music")(mission)
    local entry = required(mission.states.STATE_80B3C09E_0008_0000_80B3C09C, "powerhouse state")
    local bridge = {}
    for index, name in ipairs(BRIDGE_DEVICES) do
        bridge[index] = required(mission.Slot[name], "Slot." .. name)
    end
    local bridge_objective = required(mission.Slot.EMBER_POWERHOUSE_BRIDGE_OBJECTIVE,
                                      "bridge objective")
    local directive = required(mission.Slot.M_DIRECTIVE_SENSOR_80B3C90A, "directive sensor")
    local audience = required(mission.Slot.M_ENGAGEMENT_SENSOR_80B3CB5E, "landing engagement")
    local clear = required(mission.Directive.FIND_AND_DISABLE_THE_ALMIGHTY_S_WEAPONS_A70DA4A6,
                           "landing clear directive")
    local controls = required(mission.Directive.FIND_AND_DISABLE_THE_ALMIGHTY_S_WEAPONS_2700C0C5,
                              "bridge controls directive")
    local follow = required(mission.Directive.FIND_AND_DISABLE_THE_ALMIGHTY_S_WEAPONS_4EA80049,
                            "fuel stream directive")
    local console = required(mission.Slot.LANDING_MERCURY_CONSOLE_BUTTON_GHOST_LINK, "Ghost console")
    local lever = required(mission.Slot.LANDING_MERCURY_DOOR_BUTTON_OBJECT, "Mercury door lever")
    local lever_device = required(mission.Slot.POWERHOUSE_LANDING_MERCURY_DOOR_LEVER_DEVICE,
                                   "Mercury lever animation")
    local door = required(mission.Slot.POWERHOUSE_LANDING_MERCURY_DOOR_DEVICE, "war-beast door")
    local cross = required(mission.Directive.FIND_AND_DISABLE_THE_ALMIGHTY_S_WEAPONS_AFBF882A,
                           "cross bridge directive")
    local directives = {follow, clear, controls, cross}
    local navpoints = {
        required(mission.Slot.POWERHOUSE_DIRECTIVE_LANDING_MERCURY_CLEAR_NAV_POINT, "Mercury approach navpoint"),
        required(mission.Slot.POWERHOUSE_DIRECTIVE_LANDING_MERCURY_INTERACT_NAV_POINT, "console navpoint"),
        required(mission.Slot.HELIPAD_SUN_SQUAD_FORMATION_NAV_POINT, "helipad navpoint"),
    }
    local function show_directive(context, id)
        context:set_variable("ember.directive", id)
        context:set_variable("ember.navpoint", 0)
        context:slot(directive):set_directive{directive = directives[id], audience = context:slot(audience)}
    end
    local function dialogue(context, cue)
        context:slot(mission.Slot.M_DIALOG_SENSOR_80B3C90A):play_dialogue_cue{
            cue = required(mission.DialogueCue.M_DIALOG_SENSOR_80B3C90A["CUE_" .. cue], "dialogue cue"),
        }
    end
    local encounters, transport = {}, nil
    for _, group in ipairs(GROUP_ORDER) do
        local names = ROSTER[group]
        local squads = {}
        for index, name in ipairs(names) do
            -- Progression membership changes; their authored AI objective stays intact.
            local objective = (name == "BRIDGE_VIGNETTE_SUPPORT_A_SQUAD"
                or name == "BRIDGE_VIGNETTE_SUPPORT_B_SQUAD") and OBJECTIVES.bridge or OBJECTIVES[group]
            squads[index] = {
                name = name,
                id = required(mission.Squad[name], "Squad." .. name),
                sensor = required(mission.Slot[name], "Slot." .. name),
                objective = required(mission.Slot[objective[1]], "combat objective"),
                task_groups = objective[2],
            }
        end
        -- Retain the existing Mercury state key across a development-script reload.
        local key = group == "mercury" and "ember.landing" or "ember." .. group
        encounters[group] = Encounter.new(key, squads, function(context, state)
            if group == "mercury" then
                -- Earlier skipped volumes may first arrive while backtracking. They
                -- cannot replay the landing instruction after this combat is complete.
                context:set_variable("ember.route.progress", math.max(state:variable("ember.route.progress") or 0, 5))
                show_directive(context, 3)
                dialogue(context, 3)
                transport.prepare(context, state)
                context:slot(console):set_ghost_link{generation = 2, enabled = true}
                context:set_variable("ember.console.enabled", true)
                context:set_phase(3)
                -- Console completion, not combat clear, must extend the bridge.
            end
        end, group == "passengers")
    end
    transport = require("mission_ember.transport")(mission, encounters.passengers)
    for _, step in ipairs(ROUTE) do
        required(mission.Slot[step.trigger], "Slot." .. step.trigger)
    end

    local function update_darkness(context, state)
        if state:variable("ember.later.owns_hud") then return end
        if not state:variable("ember.route.initialized") then return end
        -- Helipad squads belong to the later section, ~450m south of this fight.
        -- Their preloaded population cannot hold the bridge's restriction open.
        local complete = true
        for _, group in ipairs({"bridge", "passengers", "sun"}) do
            if encounters[group]:phase(state) ~= 2 then complete = false; break end
        end
        local enabled = state:variable("ember.bridge.extended") == true
            and state:variable("ember.region") == entry.region_index and not complete
        if state:variable("ember.darkness.enabled") == enabled then return end
        context:slot(required(mission.Slot.HARD_WIPE_GLOBALS, "hard-wipe globals"))
            :set_darkness_zone{enabled = enabled}
        context:set_variable("ember.darkness.enabled", enabled)
    end

    local function update_guidance(context, state)
        if state:variable("ember.later.owns_hud") then return end
        if not state:variable("ember.route.initialized") then return end
        music.update(context, state)
        local target = 0
        if state:variable("ember.region") == entry.region_index
            and (state:variable("ember.wipe.stage") or 0) < 2 then
            local combat = false
            for _, group in ipairs({"catwalk_entry", "catwalk_mid", "pipe", "mercury", "mercury_bonus"}) do
                if encounters[group]:phase(state) == 1 then combat = true end
            end
            if state:variable("ember.bridge.extended") then
                for _, group in ipairs({"bridge", "passengers", "sun"}) do
                    if encounters[group]:phase(state) ~= 2 then combat = true end
                end
                -- The distant helipad population is preloaded with the bridge;
                -- it suppresses guidance only after the route reaches that fight.
                local at_helipad = (state:variable("ember.route.progress") or 0) >= 8
                if at_helipad then combat = true end
                if not combat then target = 3 end
            elseif not combat then
                target = state:variable("ember.console.enabled") and 2 or 1
            end
        end
        if state:variable("ember.navpoint") == target then return end
        context:set_variable("ember.navpoint", target)
        context:slot(directive):set_directive{
            directive = directives[state:variable("ember.directive") or 1],
            audience = context:slot(audience),
            navpoint = target > 0 and context:slot(navpoints[target]) or nil,
        }
    end

    local function initialize(context)
        context:slot(audience):set_engagement_state{flags = 0, revision = 1}
        for _, device in ipairs(bridge) do
            -- These bridge animations use position 1 for retracted, 0 for extended.
            -- DF6C70 passes the value through; generic open/close names describe the value.
            context:slot(device):transition{transition = context.sdk.device_transitions.open, snap = true}
        end
        context:slot(bridge_objective):reset_objectives{}
        context:slot(console):set_ghost_link{generation = 1, enabled = false}
        context:slot(door):transition{transition = context.sdk.device_transitions.close, snap = true}
        context:slot(lever_device):transition{transition = context.sdk.device_transitions.close, snap = true}
        context:slot(lever):set_interactable_object{generation = 1}
        -- The Almighty's weapon is firing at the sun for the whole mission, and every region
        -- that shows the beam owns the device that drives it. Placing the beam without
        -- unlocking and powering its device leaves a dead prop.
        local beam = required(mission.Slot.POWERHOUSE_CORE_WEAPON_LASER_BEAM_DEVICE,
                              "powerhouse core weapon laser beam device")
        context:slot(beam):transition{transition = context.sdk.device_transitions.unlock}
        context:slot(beam):transition{transition = context.sdk.device_transitions.power_on}
    end

    return {
        initial_state = entry,
        region = entry.region_index,
        enter = function(context, state)
            if state:variable("ember.route.initialized") then
                return
            end
            initialize(context)
            context:set_variable("ember.route.initialized", true)
            for index, step in ipairs(ROUTE) do
                if index < 6 then context:slot(mission.Slot[step.trigger]):fire_trigger{} end
            end
            context:set_phase(2)
            encounters.catwalk_entry:enter(context, state)
            show_directive(context, 1)
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
        on_player_trigger = function(context, state, event)
            if not state:variable("ember.route.initialized") then return end
            for index, step in ipairs(ROUTE) do
                local slot = context:slot(mission.Slot[step.trigger])
                local key = "ember.route." .. step.trigger
                if event.registry_key == slot.registry_key and event.slot_type == slot.slot_type
                    and event.slot_index == slot.slot_index and not state:variable(key) then
                    -- A bridge volume can overlap the near landing. Do not consume its
                    -- one-shot receipt or start later waves before the bridge is usable.
                    if index >= 6 and not state:variable("ember.bridge.extended") then return end
                    context:set_variable(key, true)
                    if index <= (state:variable("ember.route.progress") or 0) then return end
                    context:set_variable("ember.route.progress", index)
                    -- Catch up earlier combat if a player jumped over a narrow volume.
                    if step.groups then
                        local last = step.groups[#step.groups]
                        for _, group in ipairs(GROUP_ORDER) do
                            encounters[group]:enter(context, state)
                            if group == last then break end
                        end
                    end
                    if index >= 3 and encounters.mercury:phase(state) == 1
                        and not state:variable("ember.clear.instruction") then
                        context:set_variable("ember.clear.instruction", true)
                        show_directive(context, 2)
                    end
                    if step.cue then dialogue(context, step.cue) end
                    return
                end
            end
        end,
        on_object_interacted = function(context, state, event)
            if not state:variable("ember.route.initialized") or state:variable("ember.lever.used") then return end
            local slot = context:slot(lever)
            if event.registry_key ~= slot.registry_key or event.slot_type ~= slot.slot_type
                or event.slot_index ~= slot.slot_index then return end
            context:set_variable("ember.lever.used", true)
            context:slot(lever_device):transition{transition = context.sdk.device_transitions.open}
            encounters.mercury_bonus:enter(context, state)
            context:slot(door):transition{transition = context.sdk.device_transitions.open}
        end,
        on_ghost_link_state = function(context, state, event)
            if not state:variable("ember.console.enabled") or state:variable("ember.bridge.extended") then return end
            local slot = context:slot(console)
            if event.registry_key ~= slot.registry_key or event.slot_type ~= slot.slot_type
                or event.slot_index ~= slot.slot_index or event.generation ~= (state:variable("ember.console.generation") or 2)
                or event.active or event.progress == nil or event.progress < 1 then return end
            for _, device in ipairs(bridge) do
                context:slot(device):transition{transition = context.sdk.device_transitions.close}
            end
            context:set_variable("ember.bridge.extended", true)
            update_darkness(context, state)
            -- Ship parents/cargo were prepared before Ghost became usable.
            -- Far-side hangar reinforcements start with bridge activation.
            -- Encounter state keeps later crossings and backtracking from respawning them.
            encounters.sun:enter(context, state)
            encounters.helipad:enter(context, state)
            transport.start(context, state)
            -- Arm crossing monitors when their route opens, so the approach to the
            -- console cannot consume a crossing before its encounter is eligible.
            for index = 6, #ROUTE do
                context:slot(mission.Slot[ROUTE[index].trigger]):fire_trigger{}
            end
            show_directive(context, 4)
            context:set_phase(4)
        end,
        update_darkness = update_darkness,
        update_guidance = update_guidance,
        reset_checkpoint = function(context, state)
            -- The user chose the checkpoint before scanning the console again.
            -- Completed catwalk/pipe/Mercury encounters and the bonus lever stay done.
            transport.reset(context, state)
            for _, group in ipairs({"bridge", "passengers", "sun", "helipad"}) do
                encounters[group]:reset(context)
            end
            for index = 6, #ROUTE do
                context:clear_variable("ember.route." .. ROUTE[index].trigger)
            end
            context:set_variable("ember.route.progress", 5)
            for _, device in ipairs(bridge) do
                context:slot(device):transition{transition = context.sdk.device_transitions.open, snap = true}
            end
            context:slot(bridge_objective):reset_objectives{}
            context:set_variable("ember.bridge.extended", false)
            context:set_variable("ember.darkness.enabled", false)
            context:slot(mission.Slot.HARD_WIPE_GLOBALS):set_darkness_zone{enabled = false}
            local revision = (state:variable("ember.console.generation") or 2) + 2
            context:set_variable("ember.console.generation", revision)
            transport.prepare(context, state)
            context:slot(console):set_ghost_link{generation = revision, enabled = true}
            context:set_variable("ember.console.enabled", true)
            show_directive(context, 3)
            context:set_phase(3)
        end,
        on_timer = transport.on_timer,
        on_actor_path_state = transport.on_path,
        on_squad_state = function(context, state, event)
            for _, group in ipairs(GROUP_ORDER) do
                encounters[group]:on_squad_state(context, state, event)
            end
            transport.on_squad(context, state, event)
            update_darkness(context, state)
        end,
    }
end
