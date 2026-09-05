-- Arrival cinematic and control handoff. The cinematic owns a separate authored state.
return function(mission, landing)
    local entry = assert(mission.states.STATE_80B3C09E_0006_0001_80B3C09A)
    local cinematic = assert(mission.Slot.PF_CINEMATIC_BOOKEND_CINEMATIC)
    local function phase(state) return state:variable("ember.cinematic") or 0 end
    local function finish(context, state)
        if phase(state) ~= 1 then return end
        context:set_variable("ember.cinematic", 2)
        context:slot(cinematic):set_cinematic_active{active = false}
        context:select_state(landing.initial_state)
        context:set_phase(2)
    end
    return {
        initial_state = entry,
        playable = function(state) return phase(state) == 2 end,
        client_state = function(context, state, event)
            local held = event.held_region_index or event.current_region_index
            -- Only a held-region report starts the movie; a requested destination is insufficient.
            if held == entry.region_index and phase(state) == 0 then
                context:set_variable("ember.cinematic", 1)
                context:set_phase(1)
                context:slot(cinematic):set_cinematic_active{active = true}
            end
        end,
        terminated = function(context, state, event)
            if phase(state) ~= 1 then return end
            local slot = context:slot(cinematic)
            if event.registry_key ~= slot.registry_key or event.slot_type ~= slot.slot_type
                or event.slot_index ~= slot.slot_index then return end
            finish(context, state)
        end,
    }
end
