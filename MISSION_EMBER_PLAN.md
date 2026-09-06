# 1AU remaining mission implementation plan

Authorized on 2026-09-06: finish navigation/wipes, then plan and implement the remaining
mission without further approval. The user launches the game manually. Install completed
candidates only while it is closed; preserve settings/save and make rollback backups.

## Evidence and implementation rules

Reference video: https://www.youtube.com/watch?v=PqurUhqC2CE ; saved copy
`/tmp/ember-reference/1au.mp4`. Opening cutscene reference:
https://www.youtube.com/watch?v=IUmUYHELJAY . Existing timestamp observations are in
`MISSION_EMBER_REFERENCE.md`. SDK topology, compact inventories and subsequent frame
inspection are recorded in `build/full-mission-audit/`.

Use authored squad identities, spawn rules/anchors, combat objectives, devices, objects,
navpoints, scenes, triggers and dialogue. Video timestamps describe event ordering;
progression must follow gameplay receipts rather than the recording's elapsed time.
Keep completed encounters complete across backtracking and streaming. A deliberate wipe
resets only its checkpoint, with new object/ship generations and stale-event protection.
A copied SDK declaration is not evidence that its native interaction already works.

## Execution result

All seven work packages reached an installed full-route candidate on2026-09-06.
The SDK audit and controller/wire regressions passed; the complete route, checkpoint
reset policies and ending are implemented. The native thermal/exposure and timed
escape-failure policies remain incomplete, and actual gameplay has deliberately not been
launched. See `MISSION_EMBER_STATUS.md` for exact limits and installation evidence.
The work order below records the plan used to build the candidate.

## Work order

1. **Landing navigation and wipe foundation — candidate installed.** Native directive
   target references, visible normal/darkness respawn policy, death evidence, three-second
   all-dead countdown and native membership wipe. Reset at Mercury before rescanning;
   retain completed approach and lever. Offline regressions passed. Native HUD/fade and
   co-op presentation await the user's live test.
2. **Complete the package audit before wiring later populations.** Resolve the SDK's
   ambiguous Processing spawn-rule edges and missing access/ready-room definitions.
   Extract each combat objective's task-group bounds. Map region ownership and objects
   to their native entry/model, interaction/destruction/carried state, and paired devices.
   Verify progression gates against closer video samples.
3. **Mineral Processing / link, region56 (~05:30–11:00).** Entry squads and shutters;
   obstructed grinder discovery; fusion-cell pickup, carry and receptacle; activation and
   defend waves; clearing debris and opening the exit. Include machinery/klaxon/lighting,
   dialogue and forward guidance. Give the defense its own retry checkpoint.
4. **Sunside / cinder, region40 (~11:00–20:50).** Exterior solar-exposure behavior and
   authored cover/route; Sunside squads and retreat scenes; ready rooms, tumbler,
   chamber/ascent/meat-grinder route; Foundry/Bruiser and exit machinery. Load interactable
   and environmental objects when their area becomes active. Preserve staged waves and
   their objectives; checkpoint each contained encounter. Finish at the energy-stream exit.
5. **Light's End / apex, region0 (~21:00–27:45).** Access and security squads, Interceptor
   availability, reactor east/west/core encounters. Cycle authored vent devices and expose
   the corresponding destructible targets; count actual target destruction. Progress from
   two side exchangers to the core, then fusion-cell pickup and reservoir deposit. Add
   enemy reinforcements, dialogue, objective counters, navigation and checkpoint cleanup.
6. **Escape and ending (~27:45–35:27).** Overload scene, escape route and authored ship
   handoff; identify and sequence the two final cinematic states, including native skip
   incidents. Complete Ember exactly once. Chosen gameplay after~35:54 is a separate
   mission and is outside this script.
7. **End-to-end verification and installation.** Run the mission controller against an
   SDK-backed simulated event stream: normal route, skipped narrow triggers, deaths and
   retries, duplicate receipts, carried-object recovery, streaming/backtracking, and final
   completion. Validate every referenced slot/squad/state and objective task bound. Build
   native code, run relevant wire/decoder regressions, install and SHA-verify the complete
   candidate with a backup. Record remaining live-only checks honestly.

## Initial audit findings

The generated SDK has 2,051 slot declarations, 162 squad sensors and160 squad definitions;
138 definitions pass its existing exactness/profile checks. Processing's11 defense
sensors each have two rule candidates with unresolved association;13 sensors in later
access/ready-room content have no generated squad definition. These require package
investigation before claiming every later squad is supported. One shared valid directive
and dialogue sensor carries the mission's authored text/cues across its gameplay states.

The existing landing behavior and user-confirmed Harvester doors/exit are the regression
baseline. New code must preserve the verified initial spawn, optional lever gate, pre-bridge
Ghost requirements, troop delivery delay, and first-section darkness completion.
