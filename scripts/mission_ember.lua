-- Development controller for the opening landing. The remaining mission is not wired yet.
local missions = require("missions")
local mission = require(assert(missions.MISSION_EMBER, "mission_ember SDK module is absent"))
local landing = require("mission_ember.landing")(mission)
local opening = require("mission_ember.opening")(mission, landing)

return {
    initial_state = opening.initial_state,
    on_event_cinematic_terminated = opening.terminated,
    on_event_client_state_changed = function(context, state, event)
        opening.client_state(context, state, event)
        local region = event.held_region_index or event.current_region_index
        -- A settle-only delta omits region fields; it does not mean that the player left.
        if region ~= nil then context:set_variable("ember.region", region) end
        region = state:variable("ember.region")
        if region == landing.region then
            landing.enter(context, state)
            landing.client_state(context, state, event)
        end
    end,
    on_event_squad_state = function(context, state, event)
        if state:variable("ember.region") == landing.region then
            landing.on_squad_state(context, state, event)
        end
    end,
}
