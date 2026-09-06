-- Full mission controller: arrival, Powerhouse, Processing, Sunside and Light's End.
local missions = require("missions")
local mission = require(assert(missions.MISSION_EMBER, "mission_ember SDK module is absent"))
local landing = require("mission_ember.landing")(mission)
local opening = require("mission_ember.opening")(mission, landing)
-- Lazy construction keeps the opening independent of later region initialization.
local routes
local function later() if not routes then routes = require("mission_ember.routes")(mission) end; return routes end
local wipe = require("mission_ember.wipe")(mission, landing, function(c, s) later().reset(c, s) end)
local function dispatch(method, c, s, e)
    if not wipe.active(s) and opening.playable(s) and s:variable("ember.later.owns_hud") then
        return later().dispatch(method, c, s, e)
    end
end
local function terminated(c, s, e)
    opening.terminated(c, s, e)
    if routes then routes.terminated(c, s, e) end
end

return {
    initial_state = opening.initial_state,
    on_event_cinematic_terminated = terminated,
    on_event_cinematic_skip_requested = terminated,
    on_event_client_state_changed = function(context, state, event)
        wipe.client_state(context, state, event)
        opening.client_state(context, state, event)
        local region = event.held_region_index or event.current_region_index
        -- A settle-only delta omits region fields; it does not mean that the player left.
        if region ~= nil then context:set_variable("ember.region", region) end
        region = state:variable("ember.region")
        if region == landing.region and opening.playable(state) then
            landing.enter(context, state)
            landing.client_state(context, state, event)
        end
        if opening.playable(state) and not wipe.active(state) and (region == 56 or region == 40 or region == 0 or routes) then
            later().client(context, state, event)
        end
        if opening.playable(state) then
            landing.update_darkness(context, state)
            landing.update_guidance(context, state)
        end
    end,
    on_event_player_trigger = function(context, state, event)
        dispatch("trigger", context, state, event)
        if wipe.active(state) then return end
        if state:variable("ember.region") == landing.region and opening.playable(state) then
            landing.on_player_trigger(context, state, event)
            landing.update_guidance(context, state)
        end
    end,
    on_event_object_interacted = function(context, state, event)
        dispatch("interaction", context, state, event)
        if wipe.active(state) then return end
        if state:variable("ember.region") == landing.region and opening.playable(state) then
            landing.on_object_interacted(context, state, event)
            landing.update_guidance(context, state)
        end
    end,
    on_event_ghost_link_state = function(context, state, event)
        if wipe.active(state) then return end
        if state:variable("ember.region") == landing.region and opening.playable(state) then
            landing.on_ghost_link_state(context, state, event)
            landing.update_guidance(context, state)
        end
    end,
    on_event_timer_elapsed = function(context, state, event)
        if wipe.timer(context, state, event) or wipe.active(state) then return end
        if dispatch("timer", context, state, event) then return end
        if opening.playable(state) then landing.on_timer(context, state, event) end
    end,
    on_event_actor_path_state = function(context, state, event)
        if wipe.active(state) then return end
        -- Existing flights finish and retire even when the player has left this region.
        if opening.playable(state) then
            landing.on_actor_path_state(context, state, event)
        end
    end,
    on_event_squad_state = function(context, state, event)
        if wipe.active(state) then return end
        if opening.playable(state) then
            if routes then routes.squad(context, state, event) end
            landing.on_squad_state(context, state, event)
            landing.update_guidance(context, state)
        end
    end,
    on_event_object_state = function(c, s, e) dispatch("object", c, s, e) end,
    on_event_damage_state = function(c, s, e) dispatch("damage", c, s, e) end,
    on_event_fireteam_state = wipe.fireteam,
    on_event_effect_result = wipe.effect,
}
