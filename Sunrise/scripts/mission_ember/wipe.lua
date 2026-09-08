-- Native type35 countdown followed by the membership hard-wipe handshake.
return function(mission, landing, reset_later)
    local function checkpoint(state)
        return state:variable("ember.checkpoint.region") or landing.region,
            state:variable("ember.checkpoint.hash") or 0x9C58857A
    end
    local function timer(state)
        return "ember.wipe.countdown." .. (state:variable("ember.wipe.cycle") or 0)
    end
    local function stage(state) return state:variable("ember.wipe.stage") or 0 end
    local function countdown(context, seconds)
        context:slot(mission.Slot.HARD_WIPE_GLOBALS):set_darkness_zone{enabled = true, wipe_seconds = seconds}
    end
    local function cancel(context, state)
        context:cancel_timer(timer(state))
        context:clear_variable("ember.wipe.stage")
        context:clear_variable("ember.wipe.seconds")
        context:clear_variable("ember.wipe.request")
        context:slot(mission.Slot.HARD_WIPE_GLOBALS):set_darkness_zone{
            enabled = state:variable("ember.darkness.enabled") == true}
    end
    return {
        active = function(state) return stage(state) >= 2 end,
        fireteam = function(context, state, event)
            local all_dead = event.dead_count > 0 and event.alive_count == 0 and event.unknown_count == 0
            context:set_variable("ember.wipe.all_dead", all_dead)
            if stage(state) >= 2 then return end -- Native cleanup now owns missing players.
            if not all_dead then
                if stage(state) == 1 then cancel(context, state) end
                return
            end
            if stage(state) == 0 and state:variable("ember.darkness.enabled") == true then
                context:set_variable("ember.wipe.cycle", (state:variable("ember.wipe.cycle") or 0) + 1)
                context:set_variable("ember.wipe.stage", 1)
                context:set_variable("ember.wipe.seconds", 3)
                countdown(context, 3)
                context:start_timer(timer(state), 1000)
            end
        end,
        timer = function(context, state, event)
            if event.timer_name ~= timer(state) then return false end
            if stage(state) ~= 1 then return true end
            if not state:variable("ember.wipe.all_dead") or not state:variable("ember.darkness.enabled") then
                cancel(context, state); return true
            end
            local remaining = state:variable("ember.wipe.seconds") - 1
            context:set_variable("ember.wipe.seconds", remaining)
            countdown(context, remaining)
            if remaining > 0 then context:start_timer(timer(state), 1000)
            else
                context:set_variable("ember.wipe.stage", 2)
                local region, hash = checkpoint(state)
                context:set_variable("ember.wipe.request", (context:restart_checkpoint{
                    region = region, spawn_set_hash = hash}).value)
            end
            return true
        end,
        client_state = function(context, state, event)
            if stage(state) < 2 or event.spawn_state == nil then return end
            -- Native state2 follows player teardown/fade start. Publish the clean
            -- encounter before its state4 release spawns the team at the checkpoint.
            if stage(state) == 2 and event.spawn_state >= 2 then
                local region, hash = checkpoint(state)
                if state:variable("ember.checkpoint.name") then reset_later(context, state)
                else landing.reset_checkpoint(context, state) end
                context:set_variable("ember.wipe.stage", 3)
                context:restart_checkpoint{region = region, spawn_set_hash = hash,
                    release_request = state:variable("ember.wipe.request")}
            end
            if stage(state) == 3 and event.spawn_state == 0 then cancel(context, state) end
        end,
        effect = function(context, state, event)
            if event.request_key and event.request_key.value == state:variable("ember.wipe.request")
                and event.outcome ~= "transport_staged" then cancel(context, state) end
        end,
    }
end
