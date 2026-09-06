-- The native pickup grants the cell and the receptacle owns its usability/item criteria.
-- Ownership Sense distinguishes held, dropped and missing cells. Only accepted receptacle
-- interaction completes delivery; proximity and the recording's timing cannot complete it.
return function(api, tag, pickup, receptacle, on_pickup, on_deposit)
    local function key(suffix) return "ember.carry." .. tag .. "." .. suffix end
    local function generation(s) return s:variable(key("generation")) or 1 end
    local function recovery(s) return "ember.carry.recover." .. tag .. "." .. generation(s) end
    local region = tag == "processing" and 56 or 0
    local obj = {}
    function obj.start(c, s)
        if s:variable(key("started")) then return end
        c:set_variable(key("started"), true)
        c:set_variable(key("receptacle_generation"), generation(s))
        api.slot(c, pickup):set_interactable_object{generation = generation(s), track_owner = true}
        api.slot(c, receptacle):set_interactable_object{generation = generation(s)}
    end
    function obj.interaction(c, s, e)
        if not s:variable(key("started")) or s:variable(key("done")) then return end
        if api.matches(c, e, receptacle) and e.generation == s:variable(key("receptacle_generation")) then
            -- Require a pickup receipt as well as native accepted use of the receptacle.
            if not s:variable(key("notified")) then return end
            c:set_variable(key("done"), true)
            c:cancel_timer(recovery(s))
            c:clear_variable(key("recover_armed"))
            c:clear_variable(key("recover_pending"))
            c:set_variable(key("held"), false)
            c:set_variable(key("present"), false)
            -- Native 9F19F0 destroys the old entity on a newer generation and
            -- active=false prevents replacement. Includes cells attached to a player.
            -- Keep the pickup generation for stale-event guards; a reset advances by two.
            api.slot(c, pickup):set_interactable_object{
                generation = generation(s) + 1, active = false, track_owner = true}
            on_deposit(c, s)
        elseif api.matches(c, e, pickup) and e.generation == generation(s) and not s:variable(key("notified")) then
            c:set_variable(key("notified"), true)
            c:set_variable(key("seen"), true)
            c:set_variable(key("held"), true)
            on_pickup(c, s)
        end
    end
    function obj.state(c, s, e)
        if not s:variable(key("started")) or s:variable(key("done"))
            or e.generation ~= generation(s) or not api.matches(c, e, pickup) then return end
        -- Expired cells may retain an entity for cleanup after they stop being alive.
        -- That entity cannot be carried or inserted and must not suppress recovery.
        local usable = e.present and e.alive ~= false
        c:set_variable(key("present"), usable)
        if usable then
            c:set_variable(key("seen"), true)
            c:cancel_timer(recovery(s))
            c:clear_variable(key("recover_armed"))
            if e.owner_known then c:set_variable(key("held"), e.has_owner) end
            if e.has_owner and not s:variable(key("notified")) then
                c:set_variable(key("notified"), true); on_pickup(c, s)
            end
        else
            c:set_variable(key("held"), false)
            -- Also recover when the first acknowledged generation is already missing;
            -- a late subscription must not require an earlier present/held event.
            if not s:variable(key("recover_armed")) then
                c:set_variable(key("recover_armed"), true)
                c:start_timer(recovery(s), 2000)
            end
        end
    end
    function obj.timer(c, s, e)
        if e.timer_name ~= recovery(s) then return false end
        c:clear_variable(key("recover_armed"))
        if s:variable(key("done")) or s:variable(key("present")) then return true end
        if s:variable("ember.region") ~= region then c:set_variable(key("recover_pending"), true); return true end
        c:set_variable(key("generation"), generation(s) + 2)
        c:clear_variable(key("seen")); c:clear_variable(key("notified"))
        api.slot(c, pickup):set_interactable_object{generation = generation(s), track_owner = true}
        -- Preserve the receptacle's current generation. A cell's expiry is not a new objective.
        return true
    end
    function obj.resume(c, s)
        if s:variable(key("recover_pending")) then
            c:clear_variable(key("recover_pending")); c:start_timer(recovery(s), 2000)
        end
    end
    function obj.reset(c, s)
        c:cancel_timer(recovery(s))
        api.objects(c, {pickup, receptacle}, false)
        c:set_variable(key("generation"), generation(s) + 2)
        for _, suffix in ipairs({"started", "done", "seen", "held", "present", "notified", "recover_pending", "recover_armed"}) do c:clear_variable(key(suffix)) end
    end
    function obj.held(s) return s:variable(key("held")) == true end
    function obj.done(s) return s:variable(key("done")) == true end
    return obj
end
