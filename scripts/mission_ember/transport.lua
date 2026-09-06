-- Movement and passenger delivery are separate native operations with separate receipts.
local AI = require("mission_ember.combat_ai")
local ARRIVAL_SETTLE_MS = 3000
local EXTRA_DROP_DELAY_MS = 3000
-- FNV-1("exit_25"), authored by Harvester action controller 80FE21CE.
-- Empty group requests an exact, unique name lookup in this Harvester's native action table.
local EXIT_ACTION = 0x7B0D3643
local NO_GROUP = 0x811C9DC5
return function(mission, passengers)
    local objective = assert(mission.Slot.EMBER_POWERHOUSE_BRIDGE_OBJECTIVE)
    local ships = {}
    -- A/B cross the bridge deck; C/D are fly-through ships with no bridge passengers.
    local manifest = {{1, 3}, {2, 4}, {}, {}}
    assert(#passengers.squads == 4 and passengers.reserved)
    for index, letter in ipairs({"A", "B", "C", "D"}) do
        local prefix = "BRIDGE_CROSSING_DROPSHIP_" .. letter
        ships[index] = {
            squad = assert(mission.Squad[prefix .. "_SQUAD"]),
            sensor = assert(mission.Slot[prefix .. "_SQUAD"]),
            member = assert(mission.Slot[prefix .. "_SQUAD_HARVESTER"]),
            entry = assert(mission.Slot[prefix .. "_ENTRY_SEQUENCE"]),
            exit = assert(mission.Slot[prefix .. "_EXIT_SEQUENCE"]),
            cargo = manifest[index],
        }
    end
    local function generation(state) return state:variable("ember.transport.generation") or 1 end
    local function timer_name(state, kind, index)
        local gen = generation(state)
        return "ember.transport." .. kind .. "." .. index .. (gen == 1 and "" or "." .. gen)
    end
    local function path(context, state, ship, revision)
        context:slot(ship.member):play_actor_path{generation = generation(state), revision = revision,
            path = context:slot(revision == 1 and ship.entry or ship.exit)}
    end
    -- Five bits per ship: entry finished, delivery finished, exit requested, retired, unload requested.
    local function depart(context, state, index)
        local shift = (index - 1) * 5
        local flags = state:variable("ember.transport.flags") or 0
        if ((flags >> shift) & 31) ~= 19 then return end
        local seen = state:variable(passengers.tag .. ".seen") or 0
        for _, cargoIndex in ipairs(ships[index].cargo) do
            if (seen & (1 << (cargoIndex - 1))) == 0 then return end
        end
        -- Native delivery is now prompt. Keep a brief authored-position hover after
        -- detach completes; native departure action follows the exit path with no timer.
        local holds = state:variable("ember.transport.departures") or 0
        local scheduled, ready = 1 << (index - 1), 1 << (index + 3)
        if (holds & scheduled) == 0 then
            context:set_variable("ember.transport.departures", holds | scheduled)
            context:start_timer(timer_name(state, "depart", index), 4000)
            return
        end
        if (holds & ready) == 0 then return end
        context:set_variable("ember.transport.flags", flags | (4 << shift))
        path(context, state, ships[index], 2)
    end
    local function unload(context, state, index)
        local shift = (index - 1) * 5
        local flags = state:variable("ember.transport.flags") or 0
        local previous = (flags >> shift) & 31
        if (previous & 25) ~= 1 then return end
        local cargo = {}
        for _, cargoIndex in ipairs(ships[index].cargo) do
            cargo[#cargo + 1] = context:slot(passengers.squads[cargoIndex].sensor)
        end
        if #cargo > 0 then
            context:slot(ships[index].member):deliver_squads{generation = generation(state), revision = 1, squads = cargo}
            context:set_variable("ember.transport.flags", flags | (16 << shift))
        else
            context:set_variable("ember.transport.flags", flags | (18 << shift))
            depart(context, state, index)
        end
    end
    local function prepare(context, state)
        if state:variable("ember.transport.prepared") then return end
        context:set_variable("ember.transport.prepared", true)
        -- Prepare the requests before enabling Ghost. These reserved squads and empty
        -- parents create no actors; only the later member entry command starts a ship.
        passengers:enter(context, state)
        context:set_variable("ember.transport.ai", 0)
        for _, ship in ipairs(ships) do
            local parent = context:squad(ship.squad)
            local counts = parent:counts()
            for index = 1, #parent.default_counts do counts:set(index, 0) end
            parent:place{counts = counts, mode = context.sdk.squad_modes.replace}
            -- Keep zero ordinary spawn requests: the member's path creates the ship.
            -- Objective membership lets the native squad evaluate authored behavior.
            AI.assign(context, ship.sensor, objective, -1, false)
        end
    end
    return {
        reset = function(context, state)
            local old = generation(state)
            for index, ship in ipairs(ships) do
                context:cancel_timer(timer_name(state, "depart", index))
                context:cancel_timer(timer_name(state, "unload", index))
                context:slot(ship.member):retire_actor{generation = old + 1}
            end
            context:set_variable("ember.transport.generation", old + 2)
            for _, key in ipairs({"prepared", "started", "flags", "departures", "exit_actions", "ai"}) do
                context:clear_variable("ember.transport." .. key)
            end
        end,
        prepare = prepare,
        start = function(context, state)
            if state:variable("ember.transport.started") then return end
            prepare(context, state)
            context:set_variable("ember.transport.started", true)
            context:set_variable("ember.transport.flags", 0)
            for _, ship in ipairs(ships) do path(context, state, ship, 1) end
        end,
        on_path = function(context, state, event)
            if not state:variable("ember.transport.started") or event.generation ~= generation(state) then return end
            for index, ship in ipairs(ships) do
                local member = context:slot(ship.member)
                if event.registry_key == member.registry_key and event.slot_type == member.slot_type
                    and event.slot_index == member.slot_index then
                    local bits = 0
                    if event.revision == 1 and event.path_state == 1 then bits = bits | 1 end
                    if event.delivery_revision == 1 and event.delivery_state == 0 then bits = bits | 2 end
                    local flags = state:variable("ember.transport.flags") or 0
                    local previous = (flags >> ((index - 1) * 5)) & 31
                    local exits = state:variable("ember.transport.exit_actions") or 0
                    local requested = (exits & (1 << (index - 1))) ~= 0
                    if not event.dead and event.revision == 2 and event.path_state == 1
                        and (previous & 12) == 4 and not requested then
                        context:set_variable("ember.transport.exit_actions", exits | (1 << (index - 1)))
                        member:play_actor_action{generation = generation(state), revision = 3, group = NO_GROUP, action = EXIT_ACTION}
                    end
                    if event.dead or (event.revision == 3 and event.path_state == 1 and requested) then
                        bits = bits | 8
                        if (previous & 8) == 0 then member:retire_actor{generation = generation(state) + 1} end
                    end
                    context:set_variable("ember.transport.flags", flags | (bits << ((index - 1) * 5)))
                    if (bits & 1) ~= 0 and (previous & 9) == 0 and not event.dead then
                        context:start_timer(timer_name(state, "unload", index), ARRIVAL_SETTLE_MS + EXTRA_DROP_DELAY_MS)
                    end
                    depart(context, state, index)
                    return
                end
            end
        end,
        on_timer = function(context, state, event)
            if not state:variable("ember.transport.started") then return end
            for index = 1, #ships do
                if event.timer_name == timer_name(state, "depart", index) then
                    local holds = state:variable("ember.transport.departures") or 0
                    local scheduled = 1 << (index - 1)
                    if (holds & scheduled) ~= 0 then
                        context:set_variable("ember.transport.departures", holds | (1 << (index + 3)))
                        depart(context, state, index)
                    end
                    return
                end
                if event.timer_name == timer_name(state, "unload", index) then
                    unload(context, state, index)
                    return
                end
            end
        end,
        on_squad = function(context, state, event)
            if not state:variable("ember.transport.prepared") then return end
            for index, ship in ipairs(ships) do
                local sensor = context:slot(ship.sensor)
                if event.registry_key == sensor.registry_key and event.slot_type == sensor.slot_type
                    and event.slot_index == sensor.slot_index then
                    AI.update(context, state, "ember.transport.ai", ship.sensor, objective, 14, event, index, false)
                end
                if state:variable("ember.transport.started") then depart(context, state, index) end
            end
        end,
    }
end
