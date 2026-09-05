-- Development controller for the opening landing. The remaining mission is not wired yet.
local missions = require("missions")
local mission = require(assert(missions.MISSION_EMBER, "mission_ember SDK module is absent"))
local landing = require("mission_ember.landing")(mission)

return {
    initial_state = landing.initial_state,
    on_start = function(context, state)
        landing.initialize(context)
    end,
    on_event_client_state_changed = function(context, state, event)
        local region = event.current_region_index
        if region == nil then
            context:clear_variable("ember.region")
            return
        end
        context:set_variable("ember.region", region)
        if region == landing.region then
            landing.enter(context, state)
        end
    end,
    on_event_squad_state = function(context, state, event)
        if state:variable("ember.region") == landing.region then
            landing.on_squad_state(context, state, event)
        end
    end,
}
