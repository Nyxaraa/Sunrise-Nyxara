package.path = "scripts/?.lua;" .. package.path
local AI = require("mission_ember.combat_ai")
local vars, calls = {}, {}
local state = {variable = function(_, key) return vars[key] end}
local objective = {}
local squad = {assign_combat_objective = function(_, args) calls[#calls+1] = args end}
local context = {
    set_variable = function(_,key,value) vars[key]=value end,
    slot = function(_,name) if name == "objective" then return objective else assert(name=="squad"); return squad end end,
}
local function update(revision, costs)
    AI.update(context,state,"ai","squad","objective",3,{objective_revision=revision, task_costs=costs})
end
AI.assign(context,"squad","objective",-1)
vars.ai=0
update(0,{100,20,30}); assert(#calls==1) -- stale computation
update(1,{2040,2040,2040,0}); assert(#calls==1) -- unreachable; ignore unauthored lane
update(1,{100,20,30}); assert(#calls==2 and calls[2].task_group==1 and calls[2].objective==objective)
update(1,{20,20,30}); assert(#calls==2) -- stable tie
update(1,{10,20,30}); assert(#calls==3 and calls[3].task_group==0)
update(1,{2040,2040,2040}); assert(#calls==4 and calls[4].task_group==-1)
update(1,{nil,16,2040}); assert(#calls==5 and calls[5].task_group==1)
print("combat objective cost selection tests passed")

-- Different squads share a word; updating lane 12 must preserve lane 1 exactly.
local before = vars.ai
AI.update(context,state,"ai","squad","objective",3,{objective_revision=1,task_costs={30,20,10}},12)
assert(vars.ai & 31 == before & 31)
assert((vars.ai >> 55) & 31 == 3)
assert(math.type(vars.ai)=="integer")

-- Cargo stays reserved when costs change after native passenger creation.
AI.assign(context,"squad","objective",-1,true)
assert(calls[#calls].reserved)
AI.update(context,state,"cargo","squad","objective",3,{objective_revision=1,task_costs={30,20,10}},1,true)
assert(calls[#calls].reserved and calls[#calls].task_group == 2)
