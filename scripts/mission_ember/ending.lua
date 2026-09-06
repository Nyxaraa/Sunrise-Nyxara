-- Authored post-escape cinematic regions; exact native completion/skip incidents advance once.
return function(m)
    local movies = {
        {state = m.states.STATE_80B3C09E_0000_0001_80B3C091, slot = m.Slot.PF_CINEMATIC_BOOKEND_STM_CINEMATIC},
        {state = m.states.STATE_80B3C09E_0000_0002_80B3C093, slot = m.Slot.PF_CINEMATIC_BOOKEND_CNN_CINEMATIC},
    }
    local E = {}
    local staging = assert(m.states.STATE_80B3C09E_0006_0001_80B3C09A)
    local function transit(c)
        c:set_variable("ember.ending.transit", true)
        c:select_state(staging)
    end
    local music = require("mission_ember.music")(m)
    function E.start(c, s)
        if s:variable("ember.ending") then return end
        c:set_variable("ember.ending", 1)
        music.update(c, s)
        transit(c)
    end
    function E.client(c, s, e)
        local index = s:variable("ember.ending")
        local row = index and movies[index]
        if not row or s:variable("ember.ending.playing") == index then return end
        local held = e.held_region_index or e.current_region_index
        if s:variable("ember.ending.transit") then
            if held == staging.region_index then
                c:clear_variable("ember.ending.transit")
                c:select_state(row.state)
            end
            return
        end
        if held == row.state.region_index then
            c:set_variable("ember.ending.playing", index)
            c:slot(assert(row.slot)):set_cinematic_active{active = true}
        end
    end
    function E.terminated(c, s, e)
        local index = s:variable("ember.ending")
        local row = index and movies[index]
        if not row or s:variable("ember.ending.playing") ~= index then return end
        local slot = c:slot(row.slot)
        if e.registry_key ~= slot.registry_key or e.slot_type ~= slot.slot_type or e.slot_index ~= slot.slot_index then return end
        c:set_variable("ember.ending", index + 1)
        slot:set_cinematic_active{active = false}
        if movies[index + 1] then transit(c)
        else
            c:set_variable("ember.complete", true)
            -- Native lifetime 6 enters the completion/reward branch (BEA9D0/B37100).
            c.lifetime:set{state = c.sdk.lifetime_states:at(6)}
            c:set_phase(100)
        end
    end
    return E
end
