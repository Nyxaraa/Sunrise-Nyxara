-- One encounter's progress lives in transactional mission state, never module-local tables.
local Encounter = {}
Encounter.__index = Encounter

function Encounter.new(tag, squads, cleared)
    assert(#squads > 0 and #squads <= 30, "encounter needs 1..30 squads")
    return setmetatable({tag = tag, squads = squads, cleared = cleared,
                         full = (1 << #squads) - 1}, Encounter)
end

function Encounter:phase(state)
    return state:variable(self.tag .. ".phase") or 0
end

function Encounter:enter(context, state)
    if self:phase(state) ~= 0 then
        return false
    end
    -- Resolve everything before staging the first placement. Missing SDK rows are errors.
    local handles = {}
    for index, squad in ipairs(self.squads) do
        handles[index] = context:squad(squad.id)
        local count = 0
        for _, value in ipairs(handles[index].default_counts) do
            assert(value >= 0, "squad has unresolved authored counts: " .. squad.name)
            count = count + value
        end
        assert(count > 0, "encounter squad has no authored members: " .. squad.name)
    end
    context:set_variable(self.tag .. ".seen", 0)
    context:set_variable(self.tag .. ".alive", 0)
    context:set_variable(self.tag .. ".phase", 1)
    for _, handle in ipairs(handles) do
        handle:place{mode = context.sdk.squad_modes.replace}
    end
    return true
end

function Encounter:on_squad_state(context, state, event)
    if self:phase(state) ~= 1 then
        return
    end
    local bit
    for index, squad in ipairs(self.squads) do
        local slot = context:slot(squad.sensor)
        if event.registry_key == slot.registry_key and event.slot_type == slot.slot_type
            and event.slot_index == slot.slot_index then
            bit = 1 << (index - 1)
            break
        end
    end
    if bit == nil or event.alive_count == nil then
        return
    end
    local seen = state:variable(self.tag .. ".seen") or 0
    local alive = state:variable(self.tag .. ".alive") or 0
    if event.removal_flag then
        context:set_variable(self.tag .. ".seen", seen & ~bit)
        context:set_variable(self.tag .. ".alive", alive & ~bit)
        return
    end
    -- Initial empty Sense and an unloaded squad are not evidence of a combat clear.
    if event.alive_count > 0 or (event.previous_alive_count or 0) > 0 then
        seen = seen | bit
    end
    if event.alive_count > 0 then
        alive = alive | bit
    else
        alive = alive & ~bit
    end
    context:set_variable(self.tag .. ".seen", seen)
    context:set_variable(self.tag .. ".alive", alive)
    if seen == self.full and alive == 0 then
        context:set_variable(self.tag .. ".phase", 2)
        self.cleared(context, state)
    end
end

-- Called only by a confirmed checkpoint reset, not by region transit or script reload.
function Encounter:reset(context)
    context:clear_variable(self.tag .. ".phase")
    context:clear_variable(self.tag .. ".seen")
    context:clear_variable(self.tag .. ".alive")
end

return Encounter
