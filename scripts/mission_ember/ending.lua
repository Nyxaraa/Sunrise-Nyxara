-- Ember bookends are pre-rendered movies. Keep Apex loaded while the native movie
-- player owns presentation; never tear down the gameplay world to create type-6 actors.
return function(m)
    local E = {}
    local music = require("mission_ember.music")(m)
    local function play(c, index)
        c:set_variable("ember.ending", index)
        c:clear_variable("ember.ending.playing")
        c:clear_variable("ember.ending.stopping")
        c:play_prerendered_movie{index = index}
        c:set_variable("ember.ending.polls", 0)
        c:start_timer("ember.ending.poll", 250)
    end
    function E.start(c, s)
        if s:variable("ember.ending") then return end
        music.update(c, s)
        play(c, 1)
    end
    function E.client(c, s, e)
        local index = s:variable("ember.ending")
        if not index or index > 2 then return end
        local status = c:prerendered_movie_status(index)
        if status == "playing" then
            if s:variable("ember.ending.playing") ~= index then c:set_variable("ember.ending.playing", index) end
        elseif status == "complete" then
            -- The native bridge requires real playback before it can report completion,
            -- including when a short/skip transition happens between script callbacks.
            if index == 1 then play(c, 2)
            else
                c:cancel_timer("ember.ending.poll")
                c:set_variable("ember.ending", 3)
                c:set_variable("ember.complete", true)
                -- The movie bridge observes this native lifetime receipt after both movies,
                -- allows the completion banner, then commits a guarded return to orbit.
                c.lifetime:set{state = c.sdk.lifetime_states:at(6)}
                c:set_phase(100)
            end
        elseif status == "failed" then
            c:set_variable("ember.ending.failed", true)
        end
    end
    function E.timer(c, s, e)
        if e.timer_name ~= "ember.ending.poll" then return true end
        E.client(c, s, e)
        local index = s:variable("ember.ending")
        if index and index <= 2 and not s:variable("ember.ending.failed") then
            local polls = (s:variable("ember.ending.polls") or 0) + 1
            c:set_variable("ember.ending.polls", polls)
            -- Also bound a refused delivery that never reached the native bridge.
            if polls >= 2600 then c:set_variable("ember.ending.failed", true)
            else c:start_timer("ember.ending.poll", 250) end
        end
        return true
    end
    -- Direct movies have no type-6 source: the native bridge handles local Escape,
    -- invokes the native stop routine, and waits for the decoder to stop.
    function E.skip(c, s, e) end
    -- A type-6 incident cannot complete a pre-rendered movie request.
    function E.started(c, s, e) end
    function E.terminated(c, s, e) end
    return E
end
