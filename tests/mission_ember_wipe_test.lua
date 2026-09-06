package.path = "scripts/?.lua;" .. package.path
local vars, messages, timers, calls = {}, {}, {}, {}
local state = {variable = function(_,key) return vars[key] end}
local context = {}
function context:set_variable(key,value) assert(type(value)=="number" or type(value)=="string" or type(value)=="boolean", "durable variables accept only scalars");vars[key]=value end
function context:clear_variable(key) vars[key]=nil end
function context:start_timer(name,delay) assert(delay==1000);timers[#timers+1]=name end
function context:cancel_timer(name) calls[#calls+1]="cancel:"..name end
function context:slot() return {set_darkness_zone=function(_,args) messages[#messages+1]=args end} end
function context:restart_checkpoint(args)
    assert(args.region==(vars["ember.checkpoint.region"] or 64) and args.spawn_set_hash==(vars["ember.checkpoint.hash"] or 0x9C58857A))
    calls[#calls+1]=args.release_request and "release" or "arm"
    if args.release_request then assert(args.release_request=="77") end
    return {value="77"}
end
local resets=0
local landing={region=64,reset_checkpoint=function()
    resets=resets+1;calls[#calls+1]="reset";vars["ember.darkness.enabled"]=false
end}
local wipe=require("mission_ember.wipe")({Slot={HARD_WIPE_GLOBALS=1}},landing)
local function life(alive,dead,unknown)
    wipe.fireteam(context,state,{alive_count=alive,dead_count=dead,unknown_count=unknown})
end
local function tick(name) return wipe.timer(context,state,{timer_name=name or timers[#timers]}) end
life(0,1,0);assert(#timers==0,"normal death cannot wipe")
vars["ember.darkness.enabled"]=true
life(1,1,0);life(0,1,1);assert(#timers==0,"alive/unknown teammates block wipe")
life(0,2,0);assert(#timers==1 and messages[#messages].wipe_seconds==3)
local old=timers[#timers]
life(0,2,0);assert(#timers==1,"duplicate death cannot restart countdown")
life(1,1,0);assert(not vars["ember.wipe.stage"])
life(0,2,0);assert(timers[#timers]~=old)
assert(not tick(old));assert(vars["ember.wipe.seconds"]==3,"old timer cannot consume next countdown")
tick();assert(messages[#messages].wipe_seconds==2)
tick();assert(messages[#messages].wipe_seconds==1)
tick();assert(messages[#messages].wipe_seconds==0 and wipe.active(state))
assert(calls[#calls]=="arm")
life(0,0,2);assert(wipe.active(state),"native teardown must not cancel committed wipe")
wipe.client_state(context,state,{spawn_state=0});assert(resets==0,"old idle receipt cannot reset encounter")
wipe.client_state(context,state,{spawn_state=1});assert(resets==0)
wipe.client_state(context,state,{spawn_state=2});assert(resets==1)
assert(calls[#calls-1]=="reset" and calls[#calls]=="release","reset outputs must precede native spawn release")
wipe.client_state(context,state,{spawn_state=4});assert(resets==1)
wipe.client_state(context,state,{spawn_state=0});assert(not wipe.active(state))
assert(messages[#messages].enabled==false)
wipe.client_state(context,state,{spawn_state=2});assert(resets==1)
vars["ember.darkness.enabled"]=true
life(0,1,0);tick();tick();tick()
wipe.effect(context,state,{request_key={value="77"},outcome="refused"})
assert(not wipe.active(state) and not vars["ember.wipe.stage"])
assert(messages[#messages].enabled==true,"refused restart must preserve encounter restriction")
print("wipe countdown, teammate revival, stale timers, native handshake order and refusal checks passed")

local laterResets=0
wipe=require("mission_ember.wipe")({Slot={HARD_WIPE_GLOBALS=1}},landing,function()
    laterResets=laterResets+1;calls[#calls+1]="later_reset"
end)
vars["ember.checkpoint.region"]=0;vars["ember.checkpoint.hash"]=0xDF59C25C;vars["ember.checkpoint.name"]="reactor"
life(0,1,0);tick();tick();tick()
wipe.client_state(context,state,{spawn_state=2})
assert(laterResets==1 and resets==1 and calls[#calls-1]=="later_reset" and calls[#calls]=="release")
wipe.client_state(context,state,{spawn_state=0});assert(not wipe.active(state))
print("region-zero reactor checkpoint uses the later reset provider and exact authored spawn set")
