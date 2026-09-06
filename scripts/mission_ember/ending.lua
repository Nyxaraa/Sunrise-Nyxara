-- Authored post-escape cinematic states; exact native completion/skip incidents advance once.
--
-- An authored region is `sliceSetIndex + stateOrdinal`, so apex gameplay (region 0) and both
-- ending bookends (regions 1 and 2) are sibling states of one slice set. Selecting a bookend
-- instantiates no new world: the client keeps the slice set it already holds and never reports
-- a new held region, so this must not wait for one. The earlier region-49 staging detour forced
-- a cross-slice-set round trip to manufacture that report and stalled instead.
--
-- The selection intent completes only once its own roster revision publishes, and intents are
-- dispatched in order, so the activation queued on a later callback always follows the seed.
return function(m)
    local movies = {
        {state = m.states.STATE_80B3C09E_0000_0001_80B3C091, slot = m.Slot.PF_CINEMATIC_BOOKEND_STM_CINEMATIC},
        {state = m.states.STATE_80B3C09E_0000_0002_80B3C093, slot = m.Slot.PF_CINEMATIC_BOOKEND_CNN_CINEMATIC},
    }
    local E = {}
    local music = require("mission_ember.music")(m)
    local function select_movie(c, index)
        c:set_variable("ember.ending", index)
        c:set_variable("ember.ending.selected", index)
        c:select_state(assert(movies[index].state))
    end
    function E.start(c, s)
        if s:variable("ember.ending") then return end
        music.update(c, s)
        select_movie(c, 1)
    end
    function E.client(c, s, e)
        local index = s:variable("ember.ending")
        local row = index and movies[index]
        -- Activate on a callback after the selection, never in the one that requested it.
        if not row or s:variable("ember.ending.selected") ~= index
            or s:variable("ember.ending.playing") == index then return end
        c:set_variable("ember.ending.playing", index)
        c:slot(assert(row.slot)):set_cinematic_active{active = true}
    end
    function E.terminated(c, s, e)
        local index = s:variable("ember.ending")
        local row = index and movies[index]
        if not row or s:variable("ember.ending.playing") ~= index then return end
        local slot = c:slot(row.slot)
        if e.registry_key ~= slot.registry_key or e.slot_type ~= slot.slot_type or e.slot_index ~= slot.slot_index then return end
        slot:set_cinematic_active{active = false}
        if movies[index + 1] then select_movie(c, index + 1)
        else
            c:set_variable("ember.ending", index + 1)
            c:set_variable("ember.complete", true)
            -- Native lifetime 6 enters the completion/reward branch (BEA9D0/B37100).
            c.lifetime:set{state = c.sdk.lifetime_states:at(6)}
            c:set_phase(100)
        end
    end
    return E
end
