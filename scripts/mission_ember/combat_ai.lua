-- Native objective costs select the group; the objective owns areas and task execution.
local AI = {}
function AI.assign(context, sensor, objective, group, reserved)
    context:slot(sensor):assign_combat_objective{
        objective = context:slot(objective), revision = 1, task_group = group, reserved = reserved or false,
    }
end
function AI.update(context, state, key, sensor, objective, group_count, event, ordinal, reserved)
    if event.objective_revision ~= 1 or not event.task_costs then return end
    -- Twelve five-bit group choices fit in one signed 64-bit durable Lua integer.
    -- Zero encodes unassigned; 1..24 encode native task groups 0..23.
    local shift = ((ordinal or 1) - 1) * 5
    assert(shift >= 0 and shift <= 55, "AI state lane is outside its packed word")
    local packed = state:variable(key) or 0
    local current = ((packed >> shift) & 31) - 1
    local best, cost = -1, 2040 -- native 4EB8E0's saturated/unreachable value
    for index = 1, group_count do
        local candidate = event.task_costs[index]
        if candidate and candidate >= 0 and candidate < cost then
            best, cost = index - 1, candidate
        end
    end
    -- Keep the current group on equal quantized costs to avoid needless relinking.
    if best >= 0 and current and current >= 0 and event.task_costs[current + 1] == cost then best = current end
    if current ~= best then
        AI.assign(context, sensor, objective, best, reserved)
        context:set_variable(key, (packed & ~(31 << shift)) | ((best + 1) << shift))
    end
end
return AI
