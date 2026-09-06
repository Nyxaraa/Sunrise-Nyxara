-- Post-escape bookends are distinct native packed-region entries (1 and 2).
-- Selection arms travel; offer playback only on the matching held-region receipt.
-- Never confuse an offered command, a skip request, or a failed start with completion.
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
        c:clear_variable("ember.ending.playing")
        c:clear_variable("ember.ending.runtime")
        c:clear_variable("ember.ending.stopping")
        c:select_state(assert(row.state))
    end
    function E.start(c, s)
        if s:variable("ember.ending") then return end
        music.update(c, s)
        play(c, 1)
    end
    function E.client(c, s, e)
        local index = s:variable("ember.ending")
        local row = index and movies[index]
        if not row or s:variable("ember.ending.offered") == index
            or e.held_region_index ~= row.state.region_index then return end
        c:set_variable("ember.ending.offered", index)
        c:slot(row.slot):set_cinematic_active{active = true}
    end
    local function matched(c, s, e)
        local index = s:variable("ember.ending")
        local row = index and movies[index]
        if not row then return end
        local slot = c:slot(row.slot)
        if e.registry_key ~= slot.registry_key or e.slot_type ~= slot.slot_type or e.slot_index ~= slot.slot_index then return end
        return index, slot
    end
    function E.started(c, s, e)
        local index = matched(c, s, e)
        if not index or s:variable("ember.ending.offered") ~= index or s:variable("ember.ending.playing") or type(e.runtime_object_id) ~= "string" then return end
        c:set_variable("ember.ending.playing", index)
        c:set_variable("ember.ending.runtime", e.runtime_object_id)
    end
    function E.skip(c, s, e)
        local index, slot = matched(c, s, e)
        if not index or s:variable("ember.ending.playing") ~= index
            or e.runtime_object_id ~= s:variable("ember.ending.runtime")
            or s:variable("ember.ending.stopping") then return end
        c:set_variable("ember.ending.stopping", true)
        slot:set_cinematic_active{active = false}
    end
    function E.terminated(c, s, e)
        local index, slot = matched(c, s, e)
        if not index or s:variable("ember.ending.playing") ~= index
            or e.runtime_object_id ~= s:variable("ember.ending.runtime") then return end
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
