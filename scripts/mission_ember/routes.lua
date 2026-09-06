return function(m)
    local controllers = {}
    local a = require("mission_ember.route_support")(m, function(c, s, name)
        local active = controllers[s:variable("ember.region")]
        if active and active.cleared then active.cleared(c, s, name) end
    end)
    local ending = require("mission_ember.ending")(m)
    controllers[56] = require("mission_ember.processing")(m, a)
    controllers[40] = require("mission_ember.cinder")(m, a)
    controllers[0] = require("mission_ember.apex")(m, a, ending)
    local R = {}
    function R.client(c, s, e)
        -- Native travel must reach the exact bookend before playback is offered.
        if s:variable("ember.ending") then ending.client(c, s, e); return end
        local active = controllers[s:variable("ember.region")]
        if not active then return end
        if not s:variable("ember.later.owns_hud") then
            c:set_variable("ember.later.owns_hud", true)
            a.darkness(c, s, false)
        end
        local rank = ({[56] = 1, [40] = 2, [0] = 3})[s:variable("ember.region")]
        if rank > (s:variable("ember.r.furthest") or 0) then c:set_variable("ember.r.furthest", rank) end
        local changed = s:variable("ember.r.region") ~= s:variable("ember.region")
        if changed then
            c:set_variable("ember.r.region", s:variable("ember.region"))
            if s:variable("ember.checkpoint.region") ~= s:variable("ember.region") then a.darkness(c, s, false) end
        end
        active.enter(c, s)
        if changed and active.resume then active.resume(c, s) end
        if active.guidance then active.guidance(c, s) end
    end
    function R.dispatch(method, c, s, e)
        if s:variable("ember.ending") then return end
        if method == "timer" then
            for _, region in ipairs({56, 40, 0}) do
                local controller = controllers[region]
                if controller.timer and controller.timer(c, s, e) then return true end
            end
            return false
        end
        local active = controllers[s:variable("ember.region")]
        if active and active[method] then return active[method](c, s, e) end
    end
    function R.squad(c, s, e)
        if not s:variable("ember.ending") then a.squad(c, s, e) end
    end
    function R.reset(c, s)
        local active = controllers[s:variable("ember.checkpoint.region")]
        assert(active, "checkpoint has no encounter controller")
        active.reset(c, s, s:variable("ember.checkpoint.name"))
    end
    R.terminated = ending.terminated
    R.started = ending.started
    R.skip = ending.skip
    return R
end
