package.path = "scripts/?.lua;" .. package.path
local Encounter = require("mission_ember.encounter")

local function fixture()
    local vars, calls = {}, {placed = 0, cleared = 0, retired = 0}
    local slots = {
        a = {registry_key = 5, slot_type = 1, slot_index = 0},
        b = {registry_key = 6, slot_type = 1, slot_index = 0},
    }
    local context = {sdk = {squad_modes = {replace = "replace"}}}
    local state = {variable = function(_, key) return vars[key] end}
    function context:set_variable(key, value) vars[key] = value end
    function context:clear_variable(key) vars[key] = nil end
    function context:slot(id) return assert(slots[id]) end
    function context:squad(id)
        assert(slots[id])
        return {default_counts = {1},
            counts = function() return {set = function(self, index, value)
                assert(index == 1 and value == 0); self.zero = true
            end} end,
            place = function(_, args)
            assert(args.mode == "replace")
            if args.counts then assert(args.counts.zero); calls.retired = calls.retired + 1
            else calls.placed = calls.placed + 1 end
        end}
    end
    local function make()
        return Encounter.new("test", {{name = "a", id = "a", sensor = "a"},
                                      {name = "b", id = "b", sensor = "b"}},
            function() calls.cleared = calls.cleared + 1 end)
    end
    local encounter = make()
    local function event(id, count, previous, removal, consumed)
        local slot = slots[id]
        encounter:on_squad_state(context, state, {
            registry_key = slot.registry_key, slot_type = slot.slot_type,
            slot_index = slot.slot_index, alive_count = count,
            previous_alive_count = previous, removal_flag = removal,
            slot_counts = consumed or {(count == 0 and previous == 1) and 1 or 0},
        })
    end
    return context, state, encounter, calls, event, make
end

do
    local context, state, encounter, calls, event = fixture()
    assert(encounter:enter(context, state))
    assert(not encounter:enter(context, state))
    assert(calls.placed == 2)
    event("a", 0, 0); event("b", 0, 0)
    assert(calls.cleared == 0)
    event("a", 1, 0); event("b", 1, 0)
    event("a", 0, 1); event("a", 0, 1)
    assert(calls.cleared == 0)
    -- Actual native death reports carry this flag; discarding them deadlocks combat.
    event("b", 0, 1, true)
    assert(calls.cleared == 1 and encounter:phase(state) == 2 and calls.retired == 2)
    assert(not encounter:enter(context,state) and calls.placed == 2 and calls.retired == 2)
    event("b", 0, 1)
    assert(calls.cleared == 1)
    encounter:reset(context)
    assert(encounter:enter(context, state) and calls.placed == 4)
    event("a", 0, 0); event("b", 0, 0)
    assert(calls.cleared == 1)
end

do
    local context, state, encounter, calls, event, make = fixture()
    encounter:enter(context, state)
    event("a", 1, 0)
    local reloaded = make()
    assert(not reloaded:enter(context, state) and calls.placed == 2)
    -- A death edge with a positive prior count proves a squad existed even if its spawn edge
    -- was outside the callback history. Both squads still have to be accounted for.
    event("a", 0, 1); event("b", 0, 1)
    assert(calls.cleared == 1 and calls.retired == 2)
    local completedReload = make()
    assert(not completedReload:enter(context,state) and calls.placed == 2 and calls.retired == 2)
end

do
    local context, state, encounter, calls = fixture()
    function context:squad() error("SDK binding is not runnable") end
    assert(not pcall(function() encounter:enter(context, state) end))
    assert(calls.placed == 0 and encounter:phase(state) == 0)
end

do
    local context, state, encounter, calls, event = fixture()
    encounter:enter(context, state)
    event("a", 1, 0); event("b", 1, 0)
    -- Both briefly vanish while their placement requests remain outstanding.
    event("a", 0, 1, false, {0}); event("b", 0, 1, false, {0})
    assert(calls.cleared == 0 and calls.retired == 0)
    -- A partial native arrival can recover without the script cancelling it.
    event("a", 1, 0); event("a", 0, 1, false, {1})
    assert(calls.cleared == 0)
    -- Accounting may arrive in a later delta after the zero-alive report.
    event("b", 0, 0, false, {1})
    assert(calls.cleared == 1 and calls.retired == 2)
end

print("encounter progression, replay, reload, flagged deaths and SDK refusal checks passed")
