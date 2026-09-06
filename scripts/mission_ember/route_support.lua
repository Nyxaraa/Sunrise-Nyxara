-- Shared, transactional helpers for the later mission. No progress is kept in module locals.
local Encounter = require("mission_ember.encounter")
return function(mission, on_clear)
    local api = {groups = {}}
    local music = require("mission_ember.music")(mission)
    local directives = {
        ["4746660F"] = mission.Directive.DELIVER_THE_FINAL_BLOW_TO_THE_ALMIGHTY_S_WEAPON_SYSTEMS,
        ["40FC40AD"] = mission.Directive.ESCAPE_THE_ALMIGHTY,
        ["03285502"] = mission.Directive.FIND_AND_DISABLE_THE_ALMIGHTY_S_WEAPONS,
        ["127B96D6"] = mission.Directive.FIND_AND_DISABLE_THE_ALMIGHTY_S_WEAPONS_127B96D6,
        ["2700C0C5"] = mission.Directive.FIND_AND_DISABLE_THE_ALMIGHTY_S_WEAPONS_2700C0C5,
        ["3CBFC90B"] = mission.Directive.FIND_AND_DISABLE_THE_ALMIGHTY_S_WEAPONS_3CBFC90B,
        ["4E4862BB"] = mission.Directive.FIND_AND_DISABLE_THE_ALMIGHTY_S_WEAPONS_4E4862BB,
        ["4EA80049"] = mission.Directive.FIND_AND_DISABLE_THE_ALMIGHTY_S_WEAPONS_4EA80049,
        ["57050F62"] = mission.Directive.FIND_AND_DISABLE_THE_ALMIGHTY_S_WEAPONS_57050F62,
        ["591B1D88"] = mission.Directive.FIND_AND_DISABLE_THE_ALMIGHTY_S_WEAPONS_591B1D88,
        ["62E3AEFB"] = mission.Directive.FIND_AND_DISABLE_THE_ALMIGHTY_S_WEAPONS_62E3AEFB,
        ["65D5979D"] = mission.Directive.FIND_AND_DISABLE_THE_ALMIGHTY_S_WEAPONS_65D5979D,
        ["6D8E0897"] = mission.Directive.FIND_AND_DISABLE_THE_ALMIGHTY_S_WEAPONS_6D8E0897,
        ["7FF69D75"] = mission.Directive.FIND_AND_DISABLE_THE_ALMIGHTY_S_WEAPONS_7FF69D75,
        ["883C188D"] = mission.Directive.FIND_AND_DISABLE_THE_ALMIGHTY_S_WEAPONS_883C188D,
        ["A70DA4A6"] = mission.Directive.FIND_AND_DISABLE_THE_ALMIGHTY_S_WEAPONS_A70DA4A6,
        ["AFBF882A"] = mission.Directive.FIND_AND_DISABLE_THE_ALMIGHTY_S_WEAPONS_AFBF882A,
        ["BAD7D583"] = mission.Directive.FIND_AND_DISABLE_THE_ALMIGHTY_S_WEAPONS_BAD7D583,
        ["D496059B"] = mission.Directive.FIND_AND_DISABLE_THE_ALMIGHTY_S_WEAPONS_D496059B,
        ["DF93A91C"] = mission.Directive.FIND_AND_DISABLE_THE_ALMIGHTY_S_WEAPONS_DF93A91C,
        ["ECECF63D"] = mission.Directive.FIND_AND_DISABLE_THE_ALMIGHTY_S_WEAPONS_ECECF63D,
        ["FF7AB219"] = mission.Directive.FIND_AND_DISABLE_THE_ALMIGHTY_S_WEAPONS_FF7AB219,
        ["26C3C19D"] = mission.Directive.SABOTAGE_THE_ALMIGHTY_S_WEAPON_SYSTEMS,
        ["61D2B286"] = mission.Directive.SABOTAGE_THE_ALMIGHTY_S_WEAPON_SYSTEMS_61D2B286,
        ["6C7F39DC"] = mission.Directive.SABOTAGE_THE_ALMIGHTY_S_WEAPON_SYSTEMS_6C7F39DC,
        ["9DD48A8A"] = mission.Directive.SABOTAGE_THE_ALMIGHTY_S_WEAPON_SYSTEMS_9DD48A8A,
    }
    local roster = require("mission_ember.route_roster")
    for _, row in ipairs(roster) do
        local squads = {}
        for index, name in ipairs(row.squads) do
            squads[#squads + 1] = {name = name, id = assert(mission.Squad[name], name),
                sensor = assert(mission.Slot[name], name), objective = assert(mission.Slot[row.objective], row.objective),
                task_groups = row.task_groups, fixed_task = row.fixed_tasks and row.fixed_tasks[index]}
        end
        api.groups[row.name] = Encounter.new("ember.r." .. row.name, squads,
            function(c, s) on_clear(c, s, row.name) end)
    end
    function api.slot(c, name) return c:slot(assert(mission.Slot[name], "missing Ember slot: " .. name)) end
    function api.matches(c, event, name)
        local slot = api.slot(c, name)
        return event.registry_key == slot.registry_key and event.slot_type == slot.slot_type
            and event.slot_index == slot.slot_index
    end
    function api.spawn(c, s, names)
        for _, name in ipairs(names) do assert(api.groups[name], name):enter(c, s) end
    end
    function api.clear(s, names)
        for _, name in ipairs(names) do if api.groups[name]:phase(s) ~= 2 then return false end end
        return true
    end
    function api.combat(s, names)
        for _, name in ipairs(names) do if api.groups[name]:phase(s) == 1 then return true end end
        return false
    end
    function api.reset(c, names)
        for _, name in ipairs(names) do api.groups[name]:reset(c) end
    end
    function api.squad(c, s, event)
        for _, row in ipairs(roster) do api.groups[row.name]:on_squad_state(c, s, event) end
    end
    function api.device(c, name, open, snap)
        api.slot(c, name):transition{transition = open and c.sdk.device_transitions.open or c.sdk.device_transitions.close,
            snap = snap or false}
    end
    function api.objects(c, names, active)
        -- Each is an exact type-4 entry at its package transform, including native components.
        for _, name in ipairs(names) do api.slot(c, name):set_object_active{active = active} end
    end
    function api.cue(c, s, index)
        local key = "ember.r.cue." .. index
        if s:variable(key) then return end
        c:set_variable(key, true)
        api.slot(c, "M_DIALOG_SENSOR_80B3C90A"):play_dialogue_cue{
            cue = assert(mission.DialogueCue.M_DIALOG_SENSOR_80B3C90A["CUE_" .. index])}
    end
    function api.scene(c, name) c:scene(assert(mission.Scene[name], name)):activate{} end
    function api.darkness(c, s, enabled)
        if s:variable("ember.darkness.enabled") == enabled then return end
        c:set_variable("ember.darkness.enabled", enabled)
        api.slot(c, "HARD_WIPE_GLOBALS"):set_darkness_zone{enabled = enabled}
    end
    function api.effect(c, s, effect, filter, selection, enabled)
        local key = "ember.effect." .. effect
        local revision = (s:variable(key) or 0) + 1
        c:set_variable(key, revision)
        if enabled then api.slot(c, filter):set_object_filter(selection) end
        api.slot(c, effect):set_mission_effect{filter = enabled and api.slot(c, filter) or nil,
            enabled = enabled, revision = revision}
    end
    function api.checkpoint(c, region, hash, name)
        c:set_variable("ember.checkpoint.region", region)
        c:set_variable("ember.checkpoint.hash", hash)
        c:set_variable("ember.checkpoint.name", name)
    end
    function api.directive(c, s, hash, marker, combat)
        local rank = ({[56] = 1, [40] = 2, [0] = 3})[s:variable("ember.region")] or 0
        if rank < (s:variable("ember.r.furthest") or 0) then return end
        music.update(c, s)
        local shown = combat and "" or (marker or "")
        local signature = hash .. ":" .. shown
        if s:variable("ember.r.guidance") == signature then return end
        local binding = assert(directives[hash], "missing authored directive " .. hash)
        local region = s:variable("ember.region")
        local engagement = ({[0] = "M_ENGAGEMENT_SENSOR_80B3C21C",
            [40] = "M_ENGAGEMENT_SENSOR_80B3C22D", [56] = "M_ENGAGEMENT_SENSOR_80B3C8F7"})[region]
        local audience = engagement and api.slot(c, engagement)
        if audience and not s:variable("ember.hud.audience." .. region) then
            -- Native 9F2A00 includes active players when flag bit0 is clear.
            audience:set_engagement_state{flags = 0, revision = 1}
            c:set_variable("ember.hud.audience." .. region, true)
        end
        api.slot(c, "M_DIRECTIVE_SENSOR_80B3C90A"):set_directive{
            directive = binding, audience = audience, navpoint = shown ~= "" and api.slot(c, shown) or nil}
        c:set_variable("ember.r.guidance", signature)
    end
    return api
end
