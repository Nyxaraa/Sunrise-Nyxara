package.path = "scripts/?.lua;" .. package.path
package.preload["sunrise.activity_sdk"] = function() return {} end
local m = dofile(arg[1] or "build/sdk-full-mission/sdk/lua/missions/mission_ember_80b3c09e.lua")
local slotDefs, squadDefs = {}, {}
for _, row in pairs(m.slots) do slotDefs[row.id] = row end
for _, row in pairs(m.squads) do squadDefs[row.id] = row end
local vars, timers, calls, handles, counts = {}, {}, {}, {}, {}
local peak, burst, peakBurst, peakTimers = 0, 0, 0, 0
local function record(method, name, args)
    burst = burst + 1; peakBurst = math.max(peakBurst, burst)
    assert(burst <= 63, "intent burst exceeded at " .. method .. ":" .. tostring(name))
    calls[#calls+1] = {method, name, args}
end
local c = {sdk = {device_transitions = {open = "open", close = "close", power_on = "power_on", power_off = "power_off", lock = "lock", unlock = "unlock"}, squad_modes = {replace = "replace", reserve = "reserve"},
    lifetime_states = {at = function(_, n) return n end}}}
local s = {variable = function(_, k) return vars[k] end}
function c:set_variable(k, v)
    assert(type(v) == "number" or type(v) == "boolean" or type(v) == "string", "opaque value stored")
    vars[k] = v; local n = 0; for _ in pairs(vars) do n=n+1 end; peak=math.max(peak,n); assert(n <= 512)
end
function c:clear_variable(k) vars[k] = nil end
function c:start_timer(k, ms)
    timers[k] = ms; local n=0; for _ in pairs(timers) do n=n+1 end; peakTimers=math.max(n,peakTimers); assert(n<=32)
end
function c:cancel_timer(k) timers[k] = nil end
function c:set_phase(p) record("phase",p) end
function c:select_state(state) assert(state.region_index); record("select",state.region_index) end
c.lifetime = {set = function(_, args) record("lifetime",args.state) end}
local types = {set_engagement_state=70,set_music_section=11,set_mission_effect=26,set_object_filter=34,transition=23,set_interactable_object=4,set_object_active=4,watch_damage=20,
    fire_trigger=31,set_directive=68,set_darkness_zone=35,play_dialogue_cue=53,play_sequence=5,
    assign_combat_objective=1,reset_objectives=3,set_cinematic_active=6}
function c:slot(id)
    assert(id, "nil SDK slot")
    if handles[id] then return handles[id] end
    local d=assert(slotDefs[id],id)
    local h={registry_key=id:sub(6,13),slot_type=d.type,slot_index=d.index}
    for method,t in pairs(types) do h[method]=function(_,args)
        assert(d.type==t,method.." wrong type "..d.type..":"..d.name)
        if method=='set_directive' and args.navpoint then assert(args.navpoint.slot_type==47) end
        if method=='watch_damage' then assert(args.target.slot_type==4 and args.revision>0) end
        if method=='assign_combat_objective' then assert(args.objective.slot_type==3) end
        record(method,d.name,args)
    end end
    handles[id]=h;return h
end
function c:squad(id)
    local d=assert(squadDefs[id],id); local values={}
    for _,member in ipairs(d.members) do values[#values+1]=member.count or member.default_count or 1 end
    local h={default_counts=values}
    function h:counts() local out={};function out:set(i,v) out[i]=v end;return out end
    function h:place(args) counts[id]=args.counts or values; record('squad',id,args) end
    return h
end
function c:scene(id) assert(id); return {activate=function() record('scene',id) end} end
function c:restart_checkpoint(args) record('checkpoint',args.region,args);return{value='9007199254740993'}end
local R=require('mission_ember.routes')(m)
local roster=require('mission_ember.route_roster')
local byname={};for _,g in ipairs(roster)do byname[g.name]=g end
local function event(name,extra)
    local h=c:slot(assert(m.Slot[name],name));local e={registry_key=h.registry_key,slot_type=h.slot_type,slot_index=h.slot_index}
    for k,v in pairs(extra or {})do e[k]=v end;return e
end
local maxInstructions = 0
local function call(fn,...)
    burst=0;local instructions=0
    debug.sethook(function()instructions=instructions+1000;assert(instructions<=100000,'callback instruction budget exceeded')end,'',1000)
    -- Production removes pattern functions even from the string metatable.
    -- Restrict callbacks too: a normal system Lua interpreter hid a live foundry fault.
    local removed={}
    for _,name in ipairs({'dump','find','match','gmatch','gsub'})do removed[name]=string[name];string[name]=nil end
    local result=table.pack(pcall(fn,...))
    for name,value in pairs(removed)do string[name]=value end
    debug.sethook();maxInstructions=math.max(maxInstructions,instructions)
    assert(result[1], result[2])
    return table.unpack(result,2,result.n)
end
local function region(n) vars['ember.region']=n;call(R.client,c,s,{held_region_index=n}) end
local function trigger(n) call(R.dispatch,'trigger',c,s,event(n)) end
local function kill(name)
    for _,n in ipairs(assert(byname[name],name).squads)do
        local id=m.Squad[n];assert(counts[id], 'unspawned squad '..n)
        local values=c:squad(id).default_counts
    call(R.squad,c,s,event(n,{alive_count=1,previous_alive_count=0,slot_counts=values}))
        call(R.squad,c,s,event(n,{alive_count=0,previous_alive_count=1,slot_counts=values}))
    end
    assert(vars['ember.r.'..name..'.phase']==2,'uncleared '..name)
end
local function use(n,g) call(R.dispatch,'interaction',c,s,event(n,{generation=g or 1}))end
local function damage(n,h,g)call(R.dispatch,'damage',c,s,event(n..'_TARGET_DAMAGE',{health=h,shield=0,revision=g or 1}))end
local function timer(prefix)
    local key;for k in pairs(timers)do if k:find(prefix,1,true)==1 then key=k;break end end
    assert(key,'no timer '..prefix);timers[key]=nil;call(R.dispatch,'timer',c,s,{timer_name=key})
end
local function copy(t) local out={};for k,v in pairs(t)do out[k]=v end;return out end
local function reset_check(name, verify)
    local savedVars, savedTimers, savedCounts=copy(vars),copy(timers),copy(counts)
    assert(vars['ember.checkpoint.name']==name)
    call(R.reset,c,s)
    verify()
    vars,timers,counts=savedVars,savedTimers,savedCounts
end
local function transition(name, position)
    for i=#calls,1,-1 do
        if calls[i][1]=='transition' and calls[i][2]==slotDefs[m.Slot[name]].name
            and (not position or calls[i][3].transition=='open' or calls[i][3].transition=='close') then return calls[i][3] end
    end
end
region(56)
assert(transition('TUMBLER_DOOR_DEVICE').transition=='open','drill entrance must reveal the rock obstruction')
kill('processing_entry');assert(vars['ember.processing.phase']==2)
do
    local savedVars,savedTimers=copy(vars),copy(timers)
    -- Late first observation: expiry must recover without a preceding pickup receipt.
    call(R.dispatch,'object',c,s,event('CARRY_OBJECT',{generation=1,present=false,alive=false}))
    timer('ember.carry.recover.processing.')
    assert(vars['ember.carry.processing.generation']==3)
    -- The expired carried cell may still have an entity awaiting native cleanup.
    call(R.dispatch,'object',c,s,event('CARRY_OBJECT',{generation=3,present=true,alive=true,owner_known=true,has_owner=true}))
    assert(next(timers)==nil,'living carried cell must not recover')
    call(R.dispatch,'object',c,s,event('CARRY_OBJECT',{generation=3,present=true,alive=false,owner_known=true,has_owner=false}))
    timer('ember.carry.recover.processing.')
    assert(vars['ember.carry.processing.generation']==5)
    use('CARRY_OBJECT',5);use('CARRY_RECEPTACLE_INTERACT_OBJECT',1)
    assert(vars['ember.carry.processing.done'])
    call(R.dispatch,'object',c,s,event('CARRY_OBJECT',{generation=5,present=false,alive=false}))
    assert(next(timers)==nil,'inserted cell must not respawn')
    vars,timers=savedVars,savedTimers
end
do
    local savedVars,savedTimers,savedCounts=copy(vars),copy(timers),copy(counts)
    use('CARRY_RECEPTACLE_INTERACT_OBJECT');assert(vars['ember.processing.phase']==2,'empty-handed console advanced')
    call(R.dispatch,'object',c,s,event('CARRY_OBJECT',{generation=1,present=true,owner_known=true,has_owner=true}))
    call(R.dispatch,'object',c,s,event('CARRY_OBJECT',{generation=1,present=false}))
    region(40);timer('ember.carry.recover.processing.')
    assert(vars['ember.carry.processing.generation']==nil,'unloaded region respawned its cell')
    region(56);timer('ember.carry.recover.processing.')
    assert(vars['ember.carry.processing.generation']==3)
    use('CARRY_OBJECT',1);use('CARRY_RECEPTACLE_INTERACT_OBJECT',1);assert(vars['ember.processing.phase']==2)
    use('CARRY_OBJECT',3);use('CARRY_RECEPTACLE_INTERACT_OBJECT',1);assert(vars['ember.processing.phase']==3)
    vars,timers,counts=savedVars,savedTimers,savedCounts
end
-- Carry pickups publish ownership; they need not have an 80804FB7 use component.
call(R.dispatch,'object',c,s,event('CARRY_OBJECT',{generation=1,present=true,alive=true,owner_known=true,has_owner=true}))
assert(vars['ember.carry.processing.notified'] and vars['ember.carry.processing.held'])
use('CARRY_RECEPTACLE_INTERACT_OBJECT')
assert(vars['ember.processing.phase']==3 and vars['ember.carry.processing.done'])
assert(not vars['ember.carry.processing.held'] and not vars['ember.carry.processing.present'])
do
    local consumed
    for i=#calls,1,-1 do
        if calls[i][1]=='set_interactable_object' and calls[i][2]==slotDefs[m.Slot.CARRY_OBJECT].name then
            consumed=calls[i][3];break
        end
    end
    assert(consumed and consumed.active==false and consumed.generation==2,
        'accepted deposit must consume the carried cell using a newer native generation')
    local before=#calls
    use('CARRY_RECEPTACLE_INTERACT_OBJECT')
    assert(#calls==before,'duplicate deposit repeated completion')
end
assert(transition('TUMBLER_DOOR_DEVICE').transition=='close' and not transition('TUMBLER_DOOR_DEVICE').snap)
assert(vars['ember.darkness.enabled'])
reset_check('processing',function()
    assert(vars['ember.processing.phase']==2 and not vars['ember.darkness.enabled'])
    assert(transition('TUMBLER_DOOR_DEVICE').transition=='open')
    assert(vars['ember.r.processing_entry.phase']==2)
    use('CARRY_RECEPTACLE_INTERACT_OBJECT',1);assert(vars['ember.processing.phase']==2)
    use('CARRY_OBJECT',3);use('CARRY_RECEPTACLE_INTERACT_OBJECT',3)
    kill('processing_wave1');kill('processing_wave2');kill('processing_wave3')
end)
kill('processing_wave1');kill('processing_wave2');kill('processing_wave3')
assert(vars['ember.processing.phase']==4 and not vars['ember.darkness.enabled'])
local before=#calls;region(56);assert(#calls==before,'processing restarted on revisit')
region(40)
assert(transition('TUMBLER_HATCH_DOOR_DEVICE').transition=='open','ready-room entrance cannot depend on enemies beyond it')
kill('ready1_entry');kill('ready1_reinforce')
trigger('TUMBLER_ENTRY_PLAYER_TRIGGER');kill('tumbler')
trigger('DECK_EAST_ENTRY_PLAYER_TRIGGER_80B3C6E8');kill('sunburn_east')
for _,n in ipairs({'sunburn_retreat_a','sunburn_retreat_b','sunburn_retreat_c'})do kill(n)end
trigger('DECK_EAST_SECRET_PLAYER_TRIGGER');kill('sunburn_secret')
trigger('DECK_BRIDGE_025_PERCENT_PLAYER_TRIGGER_80B3C6E8');kill('sunburn_bridge')
trigger('DECK_BRIDGE_100_PERCENT_PLAYER_TRIGGER');kill('sunburn_west')
trigger('DECK_WEST_EXIT_PLAYER_TRIGGER_80B3C6E8')
assert(vars['ember.cinder.phase']==5 and not vars['ember.darkness.enabled'],'outdoor deck exit started indoor darkness')
assert(not vars['ember.r.cue.28'] and not vars['ember.r.cue.20'],'indoor dialogue played outside')
trigger('DIALOG_CINDER_READY_ROOM_02_001_PLAYER_TRIGGER')
assert(vars['ember.cinder.phase']==6 and vars['ember.darkness.enabled'] and vars['ember.r.cue.28'])
trigger('DIALOG_CINDER_READY_ROOM_02_002_PLAYER_TRIGGER')
assert(vars['ember.r.cue.20'])
trigger('READY_ROOM_02_SPAWN_SET_02_PLAYER_TRIGGER')
reset_check('ready2',function()kill('ready2_entry');kill('ready2_reinforce');assert(not vars['ember.darkness.enabled'])end)
kill('ready2_entry');kill('ready2_reinforce')
assert(not vars['ember.darkness.enabled'])
trigger('CHAMBER_SPAWN_SET_01_PLAYER_TRIGGER');kill('chamber')
trigger('MEAT_GRINDER_SPAWN_SET_01_PLAYER_TRIGGER');kill('meat1')
trigger('MEAT_GRINDER_SPAWN_SET_02_PLAYER_TRIGGER');kill('meat2')
trigger('ASCENT_SPAWN_SET_01_PLAYER_TRIGGER');kill('ascent')
for i,n in ipairs({'ascent_retreat_a','ascent_retreat_b','ascent_retreat_c'})do trigger('ASCENT_SPAWN_SET_0'..(i+1)..'_PLAYER_TRIGGER');kill(n)end
trigger('FOUNDRY_SPAWN_SET_03_PLAYER_TRIGGER')
reset_check('foundry',function()kill('foundry_entry');kill('foundry_mid');kill('foundry_final');assert(vars['ember.cinder.phase']==14)end)
kill('foundry_entry');kill('foundry_mid');kill('foundry_final')
assert(vars['ember.cinder.phase']==14 and not vars['ember.darkness.enabled'])
region(0)
assert(vars['ember.r.guidance']:sub(1,8)=='4E4862BB','apex must not show the grinder objective')
assert(transition('SECURITY_DOOR_DEVICE').transition=='close')
timer('ember.apex.setup.')
for _,name in ipairs({'SPECOPS_APEX_RING_LASER_OBJECT','SPECOPS_APEX_RING_RING_OBJECT'}) do
    call(R.dispatch,'object',c,s,event(name,{generation=1,present=true,alive=true}))
end
assert(transition('SPECOPS_APEX_RING_LASER_DEVICE',true).transition=='open')
assert(transition('SPECOPS_APEX_RING_LASER_DEVICE',true).snap==false, 'startup must run effect events instead of seeking past them')
local primedCalls=#calls
call(R.dispatch,'object',c,s,event('SPECOPS_APEX_RING_LASER_OBJECT',{generation=1,present=true,alive=true}))
assert(#calls==primedCalls,'duplicate laser presence restarted its drive')
assert(vars['ember.apex.beam'] and not vars['ember.apex.surge'],'entry must light the resting beam')
assert(transition('CLAMSHELL_TO_COFFIN_EAST_BRIDGE_DEVICE', true).transition=='open')
assert(transition('CLAMSHELL_TO_COFFIN_WEST_BRIDGE_DEVICE', true).transition=='open')
assert(transition('REACTOR_COFFIN_DOOR_EAST_DEVICE').transition=='lock')
kill('access1');kill('access2');trigger('SECURITY_CENTER_PLAYER_TRIGGER')
-- Boarding the vehicle is never a substitute for defeating the two controllers.
use('SECURITY_PLACED_INTERCEPTOR_OBJECT',2)
assert(not vars['ember.apex.interceptor_boarded'])
use('SECURITY_PLACED_INTERCEPTOR_OBJECT',1)
assert(vars['ember.apex.interceptor_boarded'])
local used=#calls;use('SECURITY_PLACED_INTERCEPTOR_OBJECT',1);assert(#calls==used)
local controllers=byname.electron_controllers.squads
-- The dispenser squads hold the security room: both controller anchors are inside
-- security_center_player_trigger, while every access volume lies east of x=-331. Under the
-- access objective the native task selector walked them out through the security door, and
-- no access group could hold them, so pinning a chosen pair of groups could not work either.
assert(byname.electron_controllers.objective=='EMBER_APEX_SECURITY_OBJECTIVE'
    and byname.dispenser.objective=='EMBER_APEX_SECURITY_OBJECTIVE',
    'dispenser squads must hold the security room, not the access approach')
assert(byname.electron_controllers.fixed_tasks==nil,
    'controllers pick their own task inside the security objective')
local security=c:slot(m.Slot.EMBER_APEX_SECURITY_OBJECTIVE)
for i,n in ipairs(controllers)do
    local values=c:squad(m.Squad[n]).default_counts
    local assigned
    for _, row in ipairs(calls) do
        if row[1]=='assign_combat_objective' and row[2]==slotDefs[m.Slot[n]].name then assigned=row[3] end
    end
    assert(assigned and assigned.objective.slot_type==security.slot_type
        and assigned.objective.slot_index==security.slot_index,
        'controllers must be assigned the security objective')
    -- Cost selection runs inside that objective's seven authored groups.
    local beforeCosts=#calls
    call(R.squad,c,s,event(n,{objective_revision=1,task_costs={10,10,0,10,10,10,10}}))
    assert(#calls>beforeCosts,'controllers must follow their security task costs')
    call(R.squad,c,s,event(n,{alive_count=1,previous_alive_count=0,slot_counts=values}))
    call(R.squad,c,s,event(n,{alive_count=0,previous_alive_count=1,slot_counts=values}))
    assert((transition('SECURITY_DOOR_DEVICE').transition=='open')==(i==2), 'controller gate must require both deaths')
end
assert(vars['ember.r.dispenser.phase']==1 and vars['ember.r.security.phase']==1, 'supports must not gate the exit')
assert(vars['ember.music.section']==21)
kill('dispenser');kill('security')
trigger('APEX_DIRECTIVE_REACTOR_GOTO_PLAYER_TRIGGER')
reset_check('reactor',function()
    call(R.client,c,s,{held_region_index=0,spawn_state=0})
    assert(vars['ember.apex.phase']==3 and vars['ember.apex.generation']==3)
    damage('REACTOR_CLAMSHELL_EAST',1,1);damage('REACTOR_CLAMSHELL_EAST',0,1)
    assert(not vars['ember.apex.dead.EAST'],'stale damage crossed reset generation')
end)
for _,side in ipairs({'east','west'})do for _,wave in ipairs({'entry','reinforce','final'})do kill('reactor_'..side..'_'..wave)end end
assert(vars['ember.music.section']==22)
timer('ember.apex.explain.');assert(vars['ember.r.cue.41'] and vars['ember.music.section']==23)
assert(timers['ember.apex.vents.1']==14000)
assert(timers['ember.apex.surge_audio.1']==4000,'audio pre-roll must lead the unchanged visual surge by ten seconds')
local beforeAudio=#calls
timer('ember.apex.surge_audio.')
assert(vars['ember.apex.vent_step']=='closed' and not vars['ember.apex.surge'],'audio pre-roll changed visual timing')
local alarms=0
for i=beforeAudio+1,#calls do if calls[i][1]=='play_sequence' then alarms=alarms+1 end end
assert(alarms==2,'both surviving clamshells must pre-roll their authored audio')
timer('ember.apex.vents.') -- Surge precedes any target exposure.
assert(vars['ember.apex.vent_step']=='warning' and timers['ember.apex.vents.1']==6000)
-- The weapon fires continuously through the fight; the exposure cycle must not blink it.
-- The surge is the authored device drive of a beam that stays present and powered.
assert(vars['ember.apex.beam']==true,'the weapon must keep firing across the cycle')
-- Inverted pose, as on the landing and clamshell bridges: resting is open, the surge drives
-- to close. The opposite mapping rendered the surge as the weapon's normal state.
assert(vars['ember.apex.surge']==true,'the warning must drive the beam')
assert(transition('SPECOPS_APEX_RING_LASER_DEVICE', true).transition=='close')
assert(transition('SPECOPS_APEX_RING_RING_DEVICE', true).transition=='close')
assert(transition('REACTOR_CLAMSHELL_EAST_DOOR_A_DEVICE').transition=='close')
local coolingStart=#calls
timer('ember.apex.vents.')
assert(timers['ember.apex.vents.1']==10000)
assert(vars['ember.apex.surge']==false,'cooling shutters opened during the surge')
for i=coolingStart+1,#calls do assert(calls[i][1]~='play_sequence','opening the shutters must not restart surge audio') end
local restored={}
for i=coolingStart+1,#calls do
    local row=calls[i]
    if row[1]=='transition' then
        for _,name in ipairs({'SPECOPS_APEX_RING_LASER_DEVICE','SPECOPS_APEX_RING_RING_DEVICE'}) do
            if row[2]==slotDefs[m.Slot[name]].name and row[3].transition=='open' then restored[name]=true end
        end
        if row[2]==slotDefs[m.Slot.REACTOR_CLAMSHELL_EAST_DOOR_A_DEVICE].name and row[3].transition=='open' then
            assert(restored.SPECOPS_APEX_RING_LASER_DEVICE and restored.SPECOPS_APEX_RING_RING_DEVICE,
                'both beam devices must end the surge before opening the cooling shutters')
        end
    end
end
assert(transition('REACTOR_CLAMSHELL_EAST_LIGHT_A_DEVICE').transition=='power_on')
for _,side in ipairs({'EAST','WEST'})do
    local n='REACTOR_CLAMSHELL_'..side
    damage(n,0);assert(not vars['ember.apex.dead.'..side], 'initial zero autocompleted target')
    call(R.dispatch,'object',c,s,event(n..'_TARGET_OBJECT',{generation=1,present=true,alive=false}))
    assert(not vars['ember.apex.dead.'..side], 'initial dead object autocompleted target')
    call(R.dispatch,'object',c,s,event(n..'_TARGET_OBJECT',{generation=1,present=true,alive=true}))
    call(R.dispatch,'object',c,s,event(n..'_TARGET_OBJECT',{generation=1,present=false,alive=false}))
    assert(not vars['ember.apex.dead.'..side], 'unloaded object counted as a kill')
    if side=='EAST' then damage(n,1);damage(n,-1);assert(not vars['ember.apex.dead.EAST']);damage(n,0)
    else call(R.dispatch,'object',c,s,event(n..'_TARGET_OBJECT',{generation=1,present=true,alive=false})) end
    assert(vars['ember.apex.dead.'..side])
    if side=='EAST' then
        assert(vars['ember.apex.phase']==3 and vars['ember.music.section']==24)
        assert(transition('CLAMSHELL_TO_COFFIN_EAST_BRIDGE_DEVICE', true).transition=='open', 'one vent extended bridges')
        assert(transition('REACTOR_COFFIN_DOOR_EAST_DEVICE').transition=='lock')
    end
end
assert(timers['ember.apex.vents.1']==10000, 'changing targets restarted the exposure clock')
-- A core transition cannot publish into a different loaded area.
do
    local before=#calls;vars['ember.region']=40
    call(R.dispatch,'timer',c,s,{timer_name='ember.apex.core.1'})
    assert(#calls==before and not vars['ember.apex.core_ready'])
    vars['ember.region']=0
end
timer('ember.apex.core.')
do
    local before=#calls
    call(R.dispatch,'timer',c,s,{timer_name='ember.apex.core.1'})
    assert(#calls==before,'duplicate core timer replayed devices/objects')
end
assert(transition('CLAMSHELL_TO_COFFIN_EAST_BRIDGE_DEVICE', true).transition=='close')
assert(transition('CLAMSHELL_TO_COFFIN_WEST_BRIDGE_DEVICE', true).transition=='close')
assert(transition('REACTOR_COFFIN_DOOR_EAST_DEVICE').transition=='open')
assert(transition('REACTOR_COFFIN_DOOR_WEST_DEVICE').transition=='open')
assert(transition('REACTOR_SHIELD_DEVICE').transition=='open')
-- The new target is created inside its native housing, and cycles on the same clock.
do
    local targetSpawn=false
    for _,v in ipairs(calls)do if v[1]=='set_interactable_object' and v[2]==slotDefs[m.Slot.REACTOR_COFFIN_TARGET_OBJECT].name then targetSpawn=true end end
    assert(targetSpawn, 'central reactor target was not created')
end
timer('ember.apex.vents.')
assert(transition('REACTOR_COFFIN_DOOR_EAST_DEVICE').transition=='close')
assert(timers['ember.apex.vents.1']==14000)
timer('ember.apex.vents.')
assert(transition('REACTOR_COFFIN_DOOR_EAST_DEVICE').transition=='close')
timer('ember.apex.vents.')
assert(transition('REACTOR_COFFIN_DOOR_EAST_DEVICE').transition=='open' and vars['ember.r.cue.46'])
assert(vars['ember.music.section']==26)
for _,n in ipairs({'coffin_east','coffin_west','coffin_interior'})do kill(n)end
damage('REACTOR_COFFIN',1);damage('REACTOR_COFFIN',0)
assert(vars['ember.apex.phase']==5)
assert(not timers['ember.apex.vents.1'] and vars['ember.r.cue.48'])
assert(transition('MOTHER_BRAIN_DOOR_DEVICE').transition=='open')
use('MOTHER_BRAIN_INTERACT_OBJECT');assert(vars['ember.apex.phase']==5,'empty deposit began escape')
use('MOTHER_BRAIN_CARRY_OBJECT')
assert(vars['ember.r.guidance']:find('EMBER_DIRECTIVE_REACTOR_MOTHER_BRAIN_DELIVERY_NAV_POINT',1,true))
-- Expiry is recoverable here too, without resetting the core or its open doors.
call(R.dispatch,'object',c,s,event('MOTHER_BRAIN_CARRY_OBJECT',{generation=1,present=false,alive=false}))
timer('ember.carry.recover.apex.')
assert(vars['ember.carry.apex.generation']==3 and vars['ember.apex.phase']==5)
use('MOTHER_BRAIN_CARRY_OBJECT',1);use('MOTHER_BRAIN_INTERACT_OBJECT');assert(vars['ember.apex.phase']==5)
local depositStart=#calls
use('MOTHER_BRAIN_CARRY_OBJECT',3);use('MOTHER_BRAIN_INTERACT_OBJECT');assert(vars['ember.apex.phase']==6)
-- Publish first; only native object presence starts the movement.
assert(not vars['ember.apex.ship_started'])
call(R.dispatch,'object',c,s,event('REACTOR_GETAWAY_SHIP_OBJECT',{generation=1,present=false,alive=false}))
assert(not vars['ember.apex.ship_started'])
call(R.dispatch,'object',c,s,event('REACTOR_GETAWAY_SHIP_OBJECT',{generation=1,present=true,alive=true}))
assert(vars['ember.apex.ship_started'])
local shipSteps={}
for i=depositStart+1,#calls do
    local row=calls[i]
    if row[2]==slotDefs[m.Slot.REACTOR_GETAWAY_SHIP_OBJECT].name and row[1]=='set_object_active' and row[3].active then
        shipSteps[#shipSteps+1]='spawn'
    elseif row[2]==slotDefs[m.Slot.REACTOR_GETAWAY_SHIP_DEVICE].name and row[1]=='transition' then
        shipSteps[#shipSteps+1]=row[3].transition
    end
end
assert(table.concat(shipSteps,',')=='spawn,unlock,power_on,close,open', 'escape ship activation order')
local shipStartedCalls=#calls
call(R.dispatch,'object',c,s,event('REACTOR_GETAWAY_SHIP_OBJECT',{generation=1,present=true,alive=true}))
assert(#calls==shipStartedCalls,'duplicate presence restarted the escape flight')
assert(vars['ember.carry.apex.done'] and not vars['ember.carry.apex.held'])
local before=#calls;use('MOTHER_BRAIN_INTERACT_OBJECT');assert(#calls==before)
call(R.dispatch,'object',c,s,event('MOTHER_BRAIN_CARRY_OBJECT',{generation=3,present=false,alive=false}))
assert(not timers['ember.carry.recover.apex.3'],'consumed final cell respawned')
-- Deposit disables the scripted climb burn; native sunburn alone owns escape damage.
local detachedBeforeRail=false
for i=depositStart+1,#calls do
    if calls[i][1]=='set_mission_effect' and calls[i][3].enabled==false then detachedBeforeRail=true end
end
assert(detachedBeforeRail,'deposit must detach the pipe burn before escape')
-- No scripted rail burn is attached during escape.
call(R.dispatch,'timer',c,s,{timer_name='ember.apex.hazards'})
local escapeEffect
for i=#calls,1,-1 do
    if calls[i][1]=='set_mission_effect' then escapeEffect=calls[i][3];break end
end
assert(escapeEffect and escapeEffect.enabled==false,
    'escape must leave the additional scripted burn disabled')
for i=depositStart+1,#calls do
    assert(not (calls[i][1]=='set_mission_effect' and calls[i][3].enabled),
        'deposit must not add a second damage attachment')
end
local afterRail=#calls
call(R.dispatch,'timer',c,s,{timer_name='ember.apex.hazards'})
assert(#calls==afterRail,'duplicate hazard callback must not reattach scorch')
local sunburnState
for i=depositStart+1,#calls do
    if calls[i][1]=='set_object_active' and calls[i][2]==slotDefs[m.Slot.SUNBURN_DAMAGE_OBJECT].name then
        sunburnState=calls[i][3].active
    end
end
assert(sunburnState==true,'escape must activate its authored sunburn object')
-- The weapon is dead once the cell is in: powered off, but still installed. Deactivating the
-- ring objects would take the beam and its surrounding structure out of the world entirely.
assert(vars['ember.apex.beam']==false,'the beam must stop firing after the deposit')
assert(vars['ember.apex.surge']==false,'the drive must return to its baseline with the power')
assert(transition('SPECOPS_APEX_RING_LASER_DEVICE', true).transition=='open',
    'a dark weapon rests in its normal pose, not the surge')
local poweredOff=false
for _,row in ipairs(calls)do
    if row[1]=='transition' and row[2]=='specops_apex_ring.laser_device'
        and row[3].transition=='power_off' then poweredOff=true end
end
assert(poweredOff,'the beam must be powered off at the deposit')
-- Only the laser is the beam. Removing the core or ring would take the weapon's structure,
-- and everything built around it, out of the world.
for _,row in ipairs(calls)do
    if row[1]=='set_object_active' and row[3] and row[3].active==false then
        local n=tostring(row[2])
        assert(not (n:find('specops_apex_ring.core',1,true) or n:find('specops_apex_ring.ring',1,true)),
            'the weapon structure must never be deactivated: '..n)
    end
end
-- Each authored explosion set is armed so it can fire as the player reaches it.
for _,set in ipairs({'A','B','C','D'})do
    local name='ember_apex_explosion_sequence_prefab.explosion_set_'..set:lower()..'_player_trigger'
    local armed=false
    for _,row in ipairs(calls)do
        if row[1]=='fire_trigger' and row[2]==name then armed=true end
    end
    assert(armed,'explosion set '..set..' was never armed')
end
reset_check('escape',function()
    assert(vars['ember.apex.phase']==6 and vars['ember.apex.dead.COFFIN'])
    assert(vars['ember.apex.beam']==false,'escape restart must leave the beam off')
end)
trigger('APEX_DIRECTIVE_REACTOR_RAILS_ESCAPE_PLAYER_TRIGGER')
local sunburnAfterEscape
for i=#calls,1,-1 do
    if calls[i][1]=='set_object_active' and calls[i][2]==slotDefs[m.Slot.SUNBURN_DAMAGE_OBJECT].name then
        sunburnAfterEscape=calls[i][3].active;break
    end
end
assert(sunburnAfterEscape==false,'escape completion must disable native sunburn')
-- A selected bookend is not loaded merely because the player holds Apex's base world.
local beforeArrival=#calls
call(R.client,c,s,{held_region_index=0})
call(R.client,c,s,{current_region_index=1})
assert(#calls==beforeArrival and not vars['ember.ending.offered'],'pending/base world offered playback')
region(1)
assert(vars['ember.ending.offered']==1)
local offeredCalls=#calls;region(1);assert(#calls==offeredCalls,'duplicate arrival replayed the movie offer')
assert(vars['ember.ending']==1 and not vars['ember.ending.playing'],'offer is not native playback')
local offered=false
for _,row in ipairs(calls)do
    if row[1]=='set_cinematic_active' and row[2]=='pf_cinematic_bookend_stm._cinematic'
        and row[3].active then offered=true end
end
assert(offered,'first bookend was never offered')
local first=event('PF_CINEMATIC_BOOKEND_STM_CINEMATIC',{runtime_object_id='9007199254740993'})
local second=event('PF_CINEMATIC_BOOKEND_CNN_CINEMATIC',{runtime_object_id='9007199254740994'})
local beforeStart=#calls
call(R.terminated,c,s,first);call(R.skip,c,s,first)
assert(#calls==beforeStart and vars['ember.ending']==1,'unstarted movie advanced the ending')
call(R.started,c,s,second);assert(not vars['ember.ending.playing'],'wrong controller claimed playback')
call(R.started,c,s,first);assert(vars['ember.ending.playing']==1)
call(R.terminated,c,s,event('PF_CINEMATIC_BOOKEND_STM_CINEMATIC',{runtime_object_id='stale'}))
assert(vars['ember.ending']==1,'stale runtime object advanced the movie')
call(R.skip,c,s,first)
assert(vars['ember.ending']==1 and not vars['ember.complete'],'skip must wait for native termination')
local stoppingCalls=#calls;call(R.skip,c,s,first);assert(#calls==stoppingCalls,'repeated skip republished stop')
call(R.terminated,c,s,first)
assert(vars['ember.ending']==2 and not vars['ember.ending.playing'],'second movie is only offered')
call(R.terminated,c,s,first);assert(vars['ember.ending']==2,'old movie completion advanced its successor')
call(R.started,c,s,second);assert(not vars['ember.ending.playing'],'unoffered second movie claimed playback')
region(2)
call(R.started,c,s,second);call(R.terminated,c,s,second)
assert(vars['ember.complete'])
local objective=vars['ember.r.guidance']
region(56);region(40);assert(vars['ember.r.guidance']==objective,'backtracking reset the forward objective')
local n=#calls;call(R.terminated,c,s,event('PF_CINEMATIC_BOOKEND_CNN_CINEMATIC'));assert(#calls==n)
print('Full authored route passed: '..peak..' variables, '..peakBurst..' intents/event, '..peakTimers..' timers peak; '..maxInstructions..' Lua instructions/event (including mock)' )
