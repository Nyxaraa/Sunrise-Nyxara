-- Native scene transitions own residency; Type6 authority owns movie playback.
return function(m)
    local E = {}
    local music = require("mission_ember.music")(m)
    local movies = {
        assert(m.Slot.PF_CINEMATIC_BOOKEND_STM_CINEMATIC),
        assert(m.Slot.PF_CINEMATIC_BOOKEND_CNN_CINEMATIC),
    }
    local states = {
        assert(m.states.STATE_80B3C09E_0000_0001_80B3C091),
        assert(m.states.STATE_80B3C09E_0000_0002_80B3C093),
    }
    local timer = "ember.ending.deadline"
    local function current(s)
        local index = s:variable("ember.ending")
        if index and index <= 2 and not s:variable("ember.ending.failed") then return index end
    end
    local function exact(c, s, e)
        local index = current(s)
        if not index or not s:variable("ember.ending.offered") then return false end
        local slot = c:slot(movies[index])
        return e.registry_key == slot.registry_key and e.slot_type == slot.slot_type
            and e.slot_index == slot.slot_index
    end
    -- runtime_object_id varies per native notification; correlate the slot and client generation.
    local function same_generation(s, e)
        return e.source_generation == s:variable("ember.ending.source")
    end
    local function prepare(c, index)
        c:set_variable("ember.ending", index)
        for _, key in ipairs({"offered", "playing", "source", "stopping", "arrived", "selected", "play_request"}) do
            c:clear_variable("ember.ending." .. key)
        end
        c:set_variable("ember.ending.state_request", c:select_state(states[index], {wait_for_arrival = true}).value)
        c:start_timer(timer, 90000)
    end
    function E.start(c, s)
        if s:variable("ember.ending") then return end
        -- The Apex escape trigger has already disabled darkness, scorch and vent timers.
        music.update(c, s)
        prepare(c, 1)
    end
    local function offer(c, s)
        local index = current(s)
        if not index or s:variable("ember.ending.offered")
            or not s:variable("ember.ending.arrived") or not s:variable("ember.ending.selected") then return end
        c:set_variable("ember.ending.offered", true)
        c:set_variable("ember.ending.play_request",
            c:slot(movies[index]):set_cinematic_active{active = true}.value)
        -- Authority applies only on a changed revision; started confirms the native component accepted its start call.
        c:start_timer(timer, 60000)
    end
    function E.cinematic_started(c, s, e)
        if not exact(c, s, e) or s:variable("ember.ending.playing") then return end
        c:set_variable("ember.ending.playing", current(s))
        c:set_variable("ember.ending.source", e.source_generation)
        c:cancel_timer(timer)
    end
    function E.cinematic_skip_requested(c, s, e)
        if not exact(c, s, e) or not s:variable("ember.ending.playing")
            or not same_generation(s, e) or s:variable("ember.ending.stopping") then return end
        c:set_variable("ember.ending.stopping", true)
        c:slot(movies[current(s)]):set_cinematic_active{active = false}
        c:start_timer(timer, 15000)
    end
    function E.cinematic_terminated(c, s, e)
        if not exact(c, s, e) then return end
        if not s:variable("ember.ending.playing") then
            c:set_variable("ember.ending.failed", true)
            c:cancel_timer(timer)
            return
        end
        if not same_generation(s, e) then return end
        local index = current(s)
        c:cancel_timer(timer)
        -- Termination already stopped the native movie; advance without a redundant stop request.
        if index == 1 then
            prepare(c, 2)
        else
            c:set_variable("ember.ending", 3)
            c:set_variable("ember.complete", true)
            c:set_variable("ember.ending.orbit_request",
                c.lifetime:set{state = c.sdk.lifetime_states:at(8)}.value)
            c:set_phase(100)
        end
    end
    function E.client(c, s, e)
        local index = current(s)
        if not index or s:variable("ember.ending.offered") then return end
        if e.held_region_index ~= nil then
            c:set_variable("ember.ending.arrived", e.held_region_index == states[index].region_index)
        end
        offer(c, s)
    end
    function E.effect(c, s, e)
        if not e.request_key then return end
        local key = e.request_key.value
        if key == s:variable("ember.ending.orbit_request") then
            c:set_variable("ember.ending.orbit_outcome", e.outcome)
            return
        end
        if not current(s) then return end
        if key ~= s:variable("ember.ending.state_request")
            and key ~= s:variable("ember.ending.play_request") then return end
        if e.outcome ~= "transport_staged" then
            c:set_variable("ember.ending.failed", true)
            c:cancel_timer(timer)
            return
        end
        if key == s:variable("ember.ending.state_request") then
            c:set_variable("ember.ending.selected", true)
            offer(c, s)
        end
    end
    function E.timer(c, s, e)
        if e.timer_name ~= timer then return false end
        local index = current(s)
        if not index then return true end
        c:set_variable("ember.ending.failed", true)
        if s:variable("ember.ending.offered") then
            c:slot(movies[index]):set_cinematic_active{active = false}
        end
        return true
    end
    return E
end
