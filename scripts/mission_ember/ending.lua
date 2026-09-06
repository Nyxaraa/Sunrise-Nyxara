-- Authored post-escape cinematic states; exact native completion/skip incidents advance once.
--
-- An authored region is `sliceSetIndex + stateOrdinal`, so apex gameplay (region 0) and both
-- ending bookends (regions 1 and 2) are sibling states of one slice set. Selecting a bookend
-- instantiates no new world, so no slice-set teleport is armed for it -- and that also means the
-- client never sends another region report. Waiting for one leaves the movie unstarted forever,
-- which is exactly what a run with the teleport removed showed: the state was selected and no
-- cinematic was ever enqueued.
--
-- So the activation is queued with the selection instead of on a later callback. Intents are
-- dispatched in order and a state selection completes only once its own roster revision has
-- published, so the cinematic Auth always follows the seed that carries its slot. Slot handles
-- resolve against the static SDK definition table, not the selected state, so naming the
-- bookend before its state is live is safe.
return function(m)
    local movies = {
        {state = m.states.STATE_80B3C09E_0000_0001_80B3C091, slot = m.Slot.PF_CINEMATIC_BOOKEND_STM_CINEMATIC},
        {state = m.states.STATE_80B3C09E_0000_0002_80B3C093, slot = m.Slot.PF_CINEMATIC_BOOKEND_CNN_CINEMATIC},
    }
    local E = {}
    local music = require("mission_ember.music")(m)
    local function play(c, index)
        local row = assert(movies[index])
        c:set_variable("ember.ending", index)
        c:set_variable("ember.ending.playing", index)
        c:select_state(assert(row.state))
        c:slot(assert(row.slot)):set_cinematic_active{active = true}
    end
    function E.start(c, s)
        if s:variable("ember.ending") then return end
        music.update(c, s)
        play(c, 1)
    end
    function E.terminated(c, s, e)
        local index = s:variable("ember.ending")
        local row = index and movies[index]
        if not row or s:variable("ember.ending.playing") ~= index then return end
        local slot = c:slot(row.slot)
        if e.registry_key ~= slot.registry_key or e.slot_type ~= slot.slot_type or e.slot_index ~= slot.slot_index then return end
        slot:set_cinematic_active{active = false}
        if movies[index + 1] then play(c, index + 1)
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
