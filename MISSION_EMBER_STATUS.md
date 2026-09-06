## Finale follow-up — 2026-09-06

Changes for the latest full-run reports:

- Beam warning/exposure now drives laser/ring power as well as position. The previous script only moved the devices after leaving their power continuously enabled.
- Escape enables the authored thermal hop-on over the five narrow hot-pipe volumes and the authored rail-top effect over its authored volume. Effects are removed before ending transit and re-armed on an escape checkpoint restart. Hazard setup has its own callback.
- HUD directives now name their type-70 engagement audience. Native `1009740 -> A76E90 -> 9F1290` checks membership; `9F2A00` includes active players with flag bit 0 clear. Earlier messages left this reference absent. The public Lua API validates this as type 70, independently of type-34 damage filters.
- Both ending movies transit through the existing, unplayed region-49 cinematic staging state before selecting their own region. The observed region-0 -> region-1 same-bubble teleport stalled before cinematic start; staging makes both bookend transitions cross-bubble. Movie playback still waits for held-region acknowledgement and exact completion/skip incidents.
- Electron Controllers retain individual access-objective task assignments (groups 9/10, task slots 40/41), rather than following later cost changes into other access tasks. The group-to-controller association is inferred from the authored dispenser task ordering and needs a spatial gameplay check; native combat logic remains enabled.
- The squad observation pool reuses empty records when full, preserving records that still have population or accounted slots.

Installed 18 files into the closed game at 11:24, with backup `build/finale-followup-backup-20260906-112405`. DLL SHA-256: `1ca1f37ea2735aeb0dab6d4a7522937e3cc9c66a5a09b297448253081887f97c`. Save, settings and SDK files were preserved. Manifest: `build/finale-followup-installation.json`.

Validation: Release DLL built; 21 portable C++ tests and all five Lua suites pass. The full route uses 233 durable variables, at most 61 intents and 13,000 mock-instrumented Lua instructions per callback. Added coverage for engagement reference encoding, multi-volume effect filters, fixed controller assignments, and staging arrival before ending playback. No game was launched or stopped.

**Gameplay verification remains required:** beam/screen visuals, exact Controller task geometry, escape damage amounts, visible banner refresh, and completion of both staged bookends. The automated checks validate script progression and wire shapes; they do not prove these visual/native behaviors. The tube/noclip issue was left alone as requested.

# Latest script fix: foundry callback sandbox compatibility

Installed 2026-09-06T10:46:06.446791; backup `/home/millie/Documents/Sunrise-builds/mission-ember/build/foundry-script-backup-20260906-104606`.
Only `mission_ember/cinder.lua` changed in the game. DLL/settings/save unchanged.

Live playtest fault at t617130: `cinder.lua:122: attempt to call a nil value (method 'find')`.
The production sandbox removes string pattern functions. Replaced the prefix search with
exact checks for the three foundry groups. The full-route harness now removes the same
string functions during callbacks: it reproduced the original fault, then passed after the fix.
The full route still peaks at 228 variables, 61 intents/callback, two timers and 13,000 mocked
instructions/callback. No tube behavior changes; user identified tube deaths as a noclip issue.

This run confirmed refinery cell consumption and healthy entity reclamation through the
foundry. The script fault prevented validating the reactor finale. No game launch performed.

# Latest installed: reactor finale progression and native behavior routing

Installed 2026-09-06T10:29:48.150688 — DLL `bed8bd2ac98f4f39e0229c0535540c2360b6697c609360bb3aab792a0efab8c4`.
Backup `/home/millie/Documents/Sunrise-builds/mission-ember/build/reactor-finale-backup-20260906-102948`; all 18 files hash-verified. Game closed; settings/save/SDK preserved.

[Reference review, changes and validation](MISSION_EMBER_REACTOR.md).

- Interceptor exit requires both authored Electron Controllers; supports/vehicle boarding do not gate it.
- Correct mandatory-float damage Sense decoding unblocks clamshell/central target death events.
- Retracted initial bridges, both-target extension gate, central target creation, explicit door unlocks,
  shared warning/exposure/recovery cycle, fusion-cell route and consumed deposit to escape.
- Authored later-area sequences/scenes resolve the live state while retaining the original seed lease.
- Spaced finale dialogue, carry delivery navpoint, intermediate music milestones; native beam/ring controls.
- Release DLL, 21 native tests, five Lua suites and diff checks pass. Full route peak: 228 variables,
  61 intents/callback, two timers, 13,000 mocked instructions/callback. No game launch performed.

Live confirmation still required for final-area bridge polarity, beam/screen VFX, alarm/shutter cadence,
and audible music selection. Music section mapping and pacing are reconstructed, not original recovered Lua.

# Previous installed: native entity transmission and purge sequencing

Installed 2026-09-06T09:55:13.047971 — DLL `d6bc68793c009a883a420095a7c908a2a60a71336aea204aca2e6cc51559fbd4`.
Backup `/home/millie/Documents/Sunrise-builds/mission-ember/build/replication-gate-backup-20260906-095512`; 18 files hash-verified. Settings/save/SDK preserved.
Game was closed throughout installation; launch remains manual.

09:40/09:41 read-only captures found 57 then 93 detached authority records, all waiting
for the remote host. Native entity view +8/+9/+10 was 1/1/0, pending packet list empty,
and server packet/ACK counters advancing. Membership field6 (mask_6) was omitted.
Native 4DF650 reads that mask; 1702580 passes its member bit to 1703910, which calls
16E5F30 to set parent view +178 (entity subview +10). 1712E70 refuses all transmission
while that byte is zero. The new membership encoder explicitly publishes the remote
host bit, or zero when no host is advertised. Sizes increased by 32 bits.

Also corrected msg25 sequencing: the earlier area purges freed records but logged
`incorrect epoch sequence` (first at t79981, then every area purge). Native 170B030
requires the entity manager's own epoch +1. This is separate from msg44/common
replicationEpoch. New binding-owned authorityEpoch starts at zero, increments only
after the authenticated response is published, and refuses overflow at 255. Exact
client-requested masks are retained; msg33 remains excluded.

New 15-second `ev=entity_replication stage=census` diagnostic reports authority record
count (capacity512), detached records, enabled views, pending deletions, packet-bearing
views, and the native authority epoch. Reads only; native deletion/ACK handlers own frees.

Validation: 20 portable native tests, five Lua suites, full cross-compiled DLL and
`git diff --check` passed. No live validation of this candidate yet. Next check: kill
opening enemies and verify enabled=1 and detached/pending deletions fall, then cross
into Refinery and confirm successive purge epochs without assertions. Full mission
Bird resolution remains unconfirmed. The prior run reached reason15 at612/1024
simulation records despite successful area reclamation and fusion-cell consumption.

# Previous installed: native authority cleanup and cell consumption

Installed 2026-09-06T09:23:06.986888 — DLL `8d8772ec2c61bd60421f4f8b0600dc30697160fcf1c31040304b277a6bf8824c`.
Backup `/home/millie/Documents/Sunrise-builds/mission-ember/build/authority-cleanup-backup-20260906-092306`; 18 files hash-verified. Settings/save/SDK preserved.

Client abandoned-slot and purge requests (26/27) now receive exact-mask native purge (25)
responses with connection/session/epoch checks. Msg33 live authority transfers are excluded.
Replication epoch state advances when msg44 publishes. Valid transport ACKs release outgoing
contributions independently of unsupported incoming gameplay lanes, preventing an exhausted
packet ring from blocking further acknowledgements. Native record census logs changes every
15 seconds, including below high pressure. Accepted cell deposit explicitly consumes the pickup
with a newer inactive Auth generation; recovery stays disabled after completion.

19 portable tests, five Lua suites and cross-compiled DLL passed. Full route:221 variables,
60 intents/event,1 timer,12000 mocked instructions/event. Native reclamation/Bird resolution
and carried-cell removal **still require live confirmation**. Latest failure was1024 records,
with742 detached records in the1019-record capture. See `build/full-mission-audit/PLAYTEST-20260906.md`.

# Previous installed: native squad cleanup

Installed 2026-09-06T08:55:26.045462 — DLL `54c842f6d41c86e2ae675d8176ae0196c15c809bddb8e9b6059106c7fc05358b`.
Backup `/home/millie/Documents/Sunrise-builds/mission-ember/build/native-cleanup-backup-20260906-085525`;18 files hash-verified. Settings/save preserved. No game launch/stop.
All-zero squad requests now select native destruction (.18=0), with a fresh generation,
instead of the previous kill-only path (.18=1). Covers existing encounter retirements and
checkpoint resets; positive spawn requests are unchanged. Native simulation-record usage
is logged at high pressure and on recovery.17 portable tests/build passed; Bird resolution
requires live test. Pool remains1024. Refinery/Sunside fixes below remain installed.

Main objective wording remains authored: Find and disable... until reactor; then Sabotage...,
Deliver the final blow..., Escape... . User was informed; no speculative HUD change applied.

# Latest installed correction: Refinery ownership/prompt and Sunside timing

Installed 2026-09-06T08:48:06.799329 — DLL `e628a3bf733b40546b459f5c944e6589f7a7f87df756292a66d12a750e0686fd`.
18 files hash-verified; backup `/home/millie/Documents/Sunrise-builds/mission-ember/build/refinery-fix-backup-20260906-084806`. Settings/save preserved; no game launch/stop.
Cell ownership decoding now uses required raw64 (80809ACC flags0x21 are custom, not optional).
Removed inverted enable_use override: F32CD0 confirms native+704=false permits unfiltered use.
Sunside indoor checkpoint/darkness/dialogue moved beyond the far door; outdoor spawn preload remains.
17 portable tests and5 Lua suites passed. Needs live verification. Bird remains reason14 native
simulation records1024/1024; user asks about increasing pool allocation, investigation active.

# Ember implementation status

## Opening regression corrected

Installed 2026-09-06T08:15:31.534338;18 files hash-verified. Backup `/home/millie/Documents/Sunrise-builds/mission-ember/build/opening-regression-backup-20260906-081531`. Settings/save unchanged; game was closed and was not launched.

Fixed missing-Sense-payload eligibility that faulted the mission before later opening triggers.
Restored the working generic lever interaction; explicit use override is now receptacle-only.
17 native tests and opening/full-route Lua regressions pass; fresh live test required.

## Playtest corrections — installed 2026-09-06

Installed 2026-09-06T07:37:29.613768: DLL and17 Lua files, all18 SHA-256 verified.
Backup `build/playtest-fixes-backup-20260906-073729`. Manifest `build/playtest-fixes-installation.json`.
DLL `8d28a8a686cc97c32dc08c510e5b640098f7f8dba729755af37de83e8623ce45`. SDK/settings/save preserved. No game launch or stop.

Refinery/drill/cell recovery, end-of-drill hatch, interceptor-use door, objective mapping,
clamshell/core progression, and native music support are implemented and built. See
[playtest audit](build/full-mission-audit/PLAYTEST-20260906.md) for exact mechanisms and evidence.
Valid partial Sense packets now preserve complete object/damage/life receipts. Reactor targets
also accept guarded native present/alive-to-dead receipts. Clamshell power/lighting and alarm
cues are wired; music uses authored section bits with inferred milestone timing.

17 portable tests and all five Lua suites pass. The current candidate still requires live
verification of insertion, boarding, shield visuals, core progression and audible music transitions.
The earlier Bird pool-exhaustion cause remains unresolved. This is not a claim of a finished mission.

## Full-mission candidate — installed 2026-09-06T01:32:12

DLL SHA256 `1d33c97e9baec406afe12c614f11f2e81b87bfc1b24eef99b6bd01f540afb78e`. Backup `build/full-mission-backup-20260906-013148`.
All 2130 DLL, SDK and script files were staged and SHA-256 verified.
Settings/save were preserved; the game was not launched or stopped.
Installer `/tmp/install-ember-full-mission.py`; durable manifest
`build/full-mission-installation.json`.

The implemented route now continues from the verified Powerhouse through Processing,
Sunside, Foundry, Light's End, both reactor sides, core, cell overload, escape and the two
ending cinematic states. The root controller dispatches native object-use/ownership and
damage receipts. Ending completion/skip incidents advance only once; lifetime6 is selected
after the second movie, based on the native completion/reward branch.

The regenerated SDK is `build/sdk-mission-complete`. Audit:
162 squad sensors,173 runnable definitions, zero sensors without definitions. Eleven
Processing sensors legitimately point to two authored rule descriptors. Scoped source
resolution now separates campaign and arcade reuse; Lua addresses every sensor once.
The new120 later squads all retain authored combat objectives and task-group bounds.

Processing includes native cell pickup/deposit, staged defense, machinery, debris, hatch
objects and exit. Cinder includes ready rooms, exterior/retreat squads, chamber/meat-grinder/
ascent, Foundry and beam restrictions. Apex includes access/security, Interceptor, vent
and core objects, native damage monitoring, reinforcements, overload and escape scenes.
Cell recovery is generation-aware and waits while its region is unloaded. Missing target
health or an initial zero cannot advance destruction. Completed areas preserve the current
forward objective on backtracking.

Checkpoint sets: Mercury `9C58857A`/64; Processing `4B27745D`/56; ready room2
`82328D63`/40; Foundry `782CAF4C`/40; reactor entrance `DF59C25C`/0; escape
`45920385`/0. These are package spawn sets, not invented coordinates. Reactor reset
publishes removals first and repopulates on the first playable post-wipe client update.
The native countdown/3-versus30-second respawn policy is unchanged by the later route.

Validation: Windows DLL built;16 portable CTest tests passed; five Lua suites passed,
including the full route using the final generated SDK, every later checkpoint, skipped
narrow volumes, backtracking, lost cells, duplicate cinematic receipts and stale damage.
Full-route simulation:213 later variables (Powerhouse suite63),59/63 intents per busiest
event,12,000/100,000 Lua instructions per event including the mock,1/32 timers peak.
All162 authored squad sensors are mapped exactly once. Simulator population reports
exercise native member lanes; a simulation is not evidence of actual actors appearing.

### Remaining live validation and reconstruction limits

This is an installed full-route implementation candidate, **not a completed live playthrough**.
The user-confirmed landing baseline is preserved. Later placements, AI task choices,
pickup consumption/expiry/co-op ownership, object animations and authored spawn-set
suitability must be checked in the game. Native navpoint display, visible respawn timer,
Darkness Consumes You presentation, total wipe timing and multi-client membership also
await manual validation.

Vent timing is reconstructed18s open/8s closed, not recovered retail timing. The native
lethal target effect is applied only after a confirmed health-zero receipt; it is not an
invulnerability switch. The thermal/exposure system is incomplete: deck heat shimmer and
scoped grinder damage are wired, but sunlight-versus-cover damage, Foundry/core thermal
hazards and the timed escape-failure damage policy are not fully reconstructed. Beam
restrictions are wired; propulsion relies on the map's native behavior and remains unverified.
The optional catwalk secret lever and three skybox flyby objects retain the existing baseline.
Final STM/CNN movie ordering and lifetime6's post-movie behavior need a live check.
Do not call the whole mission retail-equivalent or fully playable until those checks pass.

Earlier status entries below are historical; the installed build above supersedes them.


## Navpoints and checkpoint wipe — installed 2026-09-06 00:04

Candidate DLL SHA256 `7e58c4d61dfb4e6996f29a59a0c340ff3bacf3ded99b55e4b5dea0c6fbb338f0`.
Backup `build/nav-wipe-backup-20260906-000436`; installer `/tmp/install-ember-nav-wipe.py`.
Nine runtime/script files installed and SHA-verified. Settings/save unchanged; no game launch/stop.

Type68 now accepts `set_directive{directive=..., navpoint=context:slot(authored_type47)}`.
Native100A6E0 consumes Auth80804F6B .11 /80804F70 .0 as the target ClientRef.
Mode2 uses native route/direct destination selection; empty markers carry the proper
811C9DC5 fallback-name sentinel. Landing Lua publishes Mercury approach, Ghost-console,
and helipad approach targets outside active encounters. Targets retire on combat/arrival,
streaming away and wipe cleanup; completed areas do not re-arm them by backtracking.
This is encounter-based combat gating, not a recovered player threat-meter edge.

The type13 Sense decoder now reports native death-with-Ghost evidence. Joined private
sessions sharing the exact destination descriptor (including its selection nonce) supply
alive/dead/unknown counts; public residents are excluded. Loading/unknown peers block
all-dead detection. This uses Sunrise's current private cohort representation; multi-client
fireteam membership and simultaneous wiping still require a live co-op test.

All dead during darkness publishes type35's native three-second countdown, then arms
the E51D80 membership hard-wipe machine. Host state1 starts it; client state2 triggers
encounter reset; client4 is held until reset outputs are staged, then host4 releases it.
The authored Mercury spawn set9C58857A replaces mission arrival for the checkpoint.
Bridge devices retract, Ghost is re-enabled at a new generation, bridge combat resets;
completed approach encounters and the lever remain complete. Rescanning starts a fresh
ship lifecycle. Per-attempt timers reject old unload/departure/countdown events.

Validation: Windows DLL build; native wire tests for navpoint encoding, player life,
visible3/30-second respawn policy and wipe3/2/1/0 timer; Lua tests for revival cancellation,
unknown-peer refusal, ordered reset/release, repeated checkpoints, old receipts and ship
replay, plus existing encounter/AI/controller regressions. The countdown timer uses
host-stepped native clamped values rather than an invented client-clock epoch.
**Native HUD marker appearance, countdown presentation, Darkness Consumes You fade,
checkpoint spawn and co-op behavior await manual gameplay validation.** Earlier entries
below are historical build notes, not the current implementation state.

## Visible respawn countdown — installed 2026-09-05 23:26

User confirmed the old delay appeared as `Finding Spawn Location`, with no visible
countdown. DC3770/DC1A20 enforce a late spawn-location hold; they do not own the HUD
respawn timer. Removed that detour and restored participation +736's bypass.

The native death-timer initializer12E9330 reads BF2480(player datum)+4, converts
IEEE half through F32350, and stores a duration at player+64. HUD1677E60 reads
that timer through12EE500 (rounded seconds) and12EE590 (fractional seconds).
BF2480 -> BF4380 resolves type13 Auth+736, as established by FA00D0's binding.
Therefore Auth80804F30 .3 ->80804F31 .3 (native+740) now explicitly carries
half30 (`4F80`) during darkness, half3 (`4200`) outside it. This adds16 body bits
only for a scripted darkness policy. The adjacent optional timer and independent
awaiting-client-sync flags are preserved. Unscripted participation bodies are unchanged.

Validation: Windows DLL build, darkness-zone wire regression covering both durations,
loading suppression, release/re-entry, unscripted behavior and following-field alignment;
Lua controller regression; diff check. Installed eight runtime/script files with SHA
verification and a backup, preserving settings/save. No game launched or stopped.
**The visible countdown needs the user's manual test. Checkpoint wipe is still unfinished.**

DLL SHA256 `30e0b6738d9a59d467857511d95a02dbe6652ae28c9b7a4470022f3021d2d0b6`.
Backup `build/visible-respawn-backup-20260905-232608`.
Installer `/tmp/install-ember-visible-respawn.py`; manifest `/tmp/ember-visible-respawn-manifest.json`.

Wipe investigation: F9CC60 publishes type13 Sense80804F2F .0 ->808094E4 .5
(nativeSense+11) from player+2112==-1 AND a valid handle from352310(player+80).
This is stronger death evidence than missing actors during loading. No player_died4053
incident was present in the latest archived run. Existing VM peers are destination peers,
not proven fireteam members; do not blindly use their count for a fireteam wipe.
Hard-wipe globals still publish mode1=-1 and an inactive timer. An actual checkpoint
reset must also rearm the relevant encounter, ship generations and timers exactly once;
ordinary region return remains one-and-done.

## Native Harvester exit action — installed 2026-09-05 22:57

User confirmed ship exits work in the subsequent manual test. Archived log:
`build/first-encounter-audit/ship-exit-respawn-test-20260905.log`.
All four ships logged native action7B0D3643/index4 starting; live metadata uniquely
resolved group07EBF354. The countdown correction does not change ship behavior.

Recovered exact FNV-1 names in resource `80FE21CE`:
`enter_90` 4182A2AF / index0, `enter_25` 4A82B0FF / index1,
`enter_45` 4482A76D / index2; `exit_90` 820D414B / index3,
`exit_25` 7B0D3643 / index4, `exit_45` 7D0D39A9 / index5.
This replaces the earlier unmapped-action hypothesis. The shallow `exit_25` is selected
and the user has now confirmed the departure works.

Lua now sends one `play_actor_action` at second-path completion. The actor generation
stays1; action revision advances from2 to3. It retires at revision3 completion or death,
not revision2 completion. A separate four-bit variable prevents repeats after duplicate
receipts or script reload. The existing6-second unload delay and4-second departure hold,
A/B manifests, empty C/D ships, working doors and encounter progression are retained.

Native wire: root80807DA1, .6 one kind9 command,254 bits. It carries group, action,
empty additional identity, absent spatial target and mode/marker -1. Composition keeps
the completed delivery request and actor generation. New API is documented in the Lua
contract and accepted by the server's bounded Auth validators.

The native consumer is A97800 -> AB4030 -> A054C0 -> A889E0. The group name is
not guessed: for an empty-group exit request on actor80FE22FC only, the A054C0 hook
searches that live actor's native metadata for exactly one group containing the authored
variant. It passes the recovered group back through the original native lookup, retaining
both native output indices. Missing/ambiguous tables fall back to original behavior.
The latest live run confirmed unique group07EBF354 resolution. All unrelated
actors/requests pass through.

Hooks1023F00 and10245C0 observe native sequence start/stop. They do not force playback,
completion, deletion, or change controller state. Logs: `harvester_exit stage=resolve`
(unique/result), `harvester_action stage=start` (active index), and `stage=stop`.
The stop log can also represent cancellation; compare it with revision3 Sense completion
and the visible effect. Exact executable prefixes guard all new detours.

Validation: Windows DLL build; mission Auth composition/action validation test including
preserved cargo/generation and malformed padding; existing path packet regression;
Lua controller (61 durable variables), encounter and AI tests; diff check. Controller
covers duplicate/reloaded path receipts, action-running receipts and one retirement per
completed action. No game launched or stopped. Eight installed files SHA-verified;
settings/save preserved.

DLL SHA256 `96601b6c1a71dd7f930499f4af39265ca08da0f9d8b69d4c53dc5f1aef1ab3d3`.
Backup `build/native-exit-backup-20260905-225739`.
Installer `/tmp/install-ember-native-exit.py`; manifest `/tmp/ember-native-exit-manifest.json`.
All-dead checkpoint wipe remains unfinished from the earlier scope; this update focuses
on the ship departure request. Earlier skip and3/30-second respawn changes remain installed.

## Respawn timing correction — installed 2026-09-05 22:34

User explicitly requires30 seconds while darkness is active and3 seconds otherwise.
DC1A20 hook now selects30/3 via native E4C110 instead of falling back to content's
possibly-negative delay. Type13 Auth retains the native timer in BOTH states when
hasDarknessPolicy is set; the previous unrestricted bypass would skip the3 seconds.
Missions without a scripted darkness policy retain their existing participation body.
Initial spawn and loading suppression remain native. All-dead checkpoint resets and
ship exit effects are still unfinished; native timing/HUD awaits the manual test.

Build, darkness-zone wire test (activation/release/re-entry/loading/unscripted cases),
mission controller regression and diff check passed. Eight installed files hash-verified;
settings/save unchanged; no game launch or stop.
DLL SHA256 `8a962d5061a3f4dd00535fe3af634ed86e360ad94efd9a32fc24919fd8314ee6`.
Backup `build/respawn-3-30-backup-20260905-223418`.
Installer `/tmp/install-ember-respawn-3-30.py`; manifest `/tmp/ember-respawn-3-30-manifest.json`.

## Escape skip and respawn-delay test build — installed 2026-09-05 22:31

Harvester doors are user-confirmed working. Ship exit visual remains unresolved;
no entrance-effect replay has been implemented. Working ship timing/cargo and
opening combat progression were retained.

`cinematic_skip`: SObject3338, FNV-1 `7352DAFE`, type17/schema808087BF.
Native1069BE0 checks the active/skippable cinematic and emits it through106A530,
the same payload builder as cinematic_started5239 and cinematic_finished1685.
Existing game logs already contained two ignored3338 requests. The host now
resolves this exact cinematic ClientRef and emits cinematicSkipRequested. Lua
uses the same idempotent stop/select-landing-state handoff as natural completion.
A later finished notification or repeated Escape cannot repeat that handoff.

The native DC3770 respawn gate compares elapsed time since player+1928 against
DC1A20's delay. New guarded darkness_respawn hook returns30 seconds while the
native E4C110 darkness flag is active, otherwise preserving the authored delay.
It keeps the native gate/timer and initial-spawn bypass. **Neither visible HUD
countdown nor multiplayer behavior has been verified. All-dead checkpoint reset
is NOT implemented in this test build; the delay alone is not a team-wipe policy.**

To verify player identity before writing a wipe reducer, bounded read-only
player_lifecycle logs retain incident payload chunks, sending session, primary
row, and lifecycle target mask. The death can be an EXTRA target of a combat
incident; do not equate sender with victim. Known targets:1121 player_spawned,
439 player_respawned,7117 player_resurrected (schema80806437),4053 player_died
(schema8080645A),5896 hard_wipe (schema808087B7). No reset is triggered from an
unverified death field. Required policy:30-second respawn while a teammate lives;
all members dead => wipe to beginning of latest objective; retain earlier cleared
sections except when deliberately resetting the selected checkpoint.

Next manual test: Escape during intro, then a death after Ghost extends the
bridge/activates darkness. Capture the lifecycle payload and actual respawn delay.
No game launch or stop by the agent.

DLL SHA256 `ccc0ed14ad0beec131a81056c7a70d3753f6f7e56b498ba4ac71249c7e5c049b`.
Full DLL build, incident payload test and three Lua suites passed; diff check clean.
Eight DLL/Lua files installed and SHA256 verified. Settings/save unchanged.
Backup `build/skip-respawn-test-backup-20260905-223142`.
Installer `/tmp/install-ember-skip-respawn.py`, manifest `/tmp/ember-skip-respawn-manifest.json`.

## Combat and Harvester doors confirmed — installed 2026-09-05 20:06

User reports all opening enemy spawns correct and darkness clears correctly. Harvester doors are now user-confirmed working. The
remaining opening blocker is the visible exit effect. Keep the successful
Lua spawn/timing/progression behavior stable. The user explicitly suggests replaying the
entrance effect before deletion as a reversible exit experiment; this is authorized, but
the actual entrance-effect trigger is NOT yet identified or implemented.

The 19:32 hook installed but emitted no per-release reports. Its filter had two incorrect
assumptions: source-root+4 is a resource-header field, not the owning actor tag; native
resolution from the live delivery instance points to class8080670A, not8080670B. The
new filter matches all four captured ships against the actual extracted resource.

Read-only manual bridge capture: `ship-runtime-20260905-195257` through `195317`.
Summary: `build/first-encounter-audit/animation-live-summary-20260905.json` (150 component
samples). Delivery advances0 ->3/published1 ->5 ->0. Its four bindings+80/+160/+240/+320
are all invalid references in live memory AND the authored80FE22FC overrides. Forcing
these actions cannot open this model's doors. Separate80FE21CE controller has13 populated
sequence bindings but active-action stayed-1 in our 1Hz samples. This does not rule out
an action shorter than the sampling interval. Mapping saved in `harvester-action-map.json`.

Found actual door names in model controller80FE22CB: source starts0x198, `dropship_doors`
at file0x368 is FNV-1 D784D6F6, and `doors` at0x388 is FNV-1 80296344. Export80FE2309
binds the former to class80803D06/feature10;80FE230A binds the latter to feature9.
These are authored names, not invented native action IDs. Exact target semantics still
require a visual test.

Replaced the empty-action experiment with a102A5B0 delivery-stage hook. Before native
stage1, request named scalar `doors=1`; before stage0 after delivery, request `doors=0`.
Use native576420(entity, &nameHash, alignedFloat4), the same route used by A9BFA0's actor
set-channel command. Source/tag checks match only80C0E5D2/class8080670A. Native stage
arguments/result, manifest, flight timing, progression and actor retirement are retained.
Log `harvester_doors` reports installation and requested values, NOT proof of visible motion.

Installed DLL SHA256 `0667a285b63e64ded6b752c1d0c9736fdfbe976fa73e04470973b9c618cbc31a`.
Build and three Lua suites passed; exact hook/helper prefixes and corrected source filter
verified using extracted binary plus all four live delivery captures; diff check clean.
Installer `/tmp/install-ember-model-doors.py`, manifest `/tmp/ember-model-doors-manifest.json`.
All eight installed files verified. Backup `build/model-doors-experiment-backup-20260905-200649`.
Game was confirmed closed; no launch or stop. Settings/save unchanged. The user has now
confirmed visible door motion. Read-only capture watcher stopped after the four ships retired.

## Forced Harvester pre-drop action — experimental build installed 2026-09-05 19:32

User explicitly authorized trying the embedded component at troop release and reverting
if it breaks. Added `client/hooks/harvester_drop/harvester_drop_experiment.cpp`, activated
only with mission scripting enabled. Exact native prologues guard the hook/helpers;
the release callback additionally checks source-root `80FE22FC` and delivery source
`80C0E5D2`/class `8080670B`. This is a temporary native experiment, not a verified door API.

Hook `102A920` immediately before native passenger release. Resolve the authored stage2
pre-drop binding at delivery runtime+240 and invoke `C7AFA0(binding, 3D30F0)` even when
zero native wait skipped that action. If previous published stage is2, native already
started it and the hook does not restart it. Invalid binding logs `unbound` and is skipped.
Keep native release arguments/result, manifest, timing and cleanup. Generic component
activation through `44E340` already occurs in native `1029330`; this experiment forces
the separate action rather than merely repeating that existing activation call.

Log event `harvester_drop_experiment` reports installation and each matched release:
`invoke`, `already_started`, `unbound`, or `exception`. Actual door motion is unverified.
Build passed; all three Lua suites passed; exact guard bytes checked against extracted
game image; diff check clean. Game was closed and was neither launched nor stopped.

Installed SHA256 `14f196a2202dfc80219dfed80308201879607df621e6e7d385fa4e99750a7dfe`.
All eight DLL/script files verified with `/tmp/ember-drop-action-manifest.json` by
`/tmp/install-ember-drop-action.py`. Backup of previous DLL/scripts/settings:
`build/drop-action-experiment-backup-20260905-193242`. Settings/save unchanged.
This installation ALSO includes the previously pending near-side squad changes,
six-second total unload timer (three additional seconds), and Bird diagnostic below.
Revert the experiment at source by removing its activation call; the backup restores
the previous installed runtime and scripts if the entire test build must be rolled back.

## Embedded Harvester controls — investigation, not a door fix

The absence of a named type23 ship-door slot does not rule out an embedded control.
The user's model-component suggestion is valid. Offline inspection found two separate
mechanisms in the native delivery component; neither is yet verified as the door animation:

- Delivery start `1029330` calls `44E340(component, source+96, true)`; state0 clears it.
  Its package binding runs through `80C0E5D2`, the `80FE22C9` instance override, and
  `815B963A` (component class `80803C23`, feature6). `44E340` resolves that binding and
  calls `590B10` with category3. The lever animation setter `DF6C70` uses sibling
  `44E3C0`, which reaches the same helper with category0 and a different binding type.
  `594CF0` changes component bit masks; `56FD30` updates entity category bitmaps.
  This is evidence of shared component-activity machinery, not evidence that category3
  or feature6 means "open doors". Do not expose a guessed door switch from these numbers.
- Delivery stages invoke bindings at runtime+80/+160/+240/+320 through `C7AFA0`.
  That function checks the resolved handle at binding+56, returns if it is invalid,
  otherwise resolves the component and invokes the supplied callback. Start callback
  `3D30F0` supplies float0 to `58E420`, which forwards to `58E260`; reset/stop callback
  `587970` clears sequence state and releases its tracked child references via `58E630`.
  Native stage replication `1028FB0` also switches these actions. These stage bindings
  are stronger animation candidates, but their actual Harvester targets still need mapping.

Evidence is in `build/first-encounter-audit/live-44e340.c`, `live-44e3c0.c`,
`live-590b10.c`, `live-594d70.c`, `live-56fd30.c`, `live-1028fb0.c`, and the raw
disassembly at the listed RVAs in `native-readable.exe`. Old slab captures contain
source/component references; do not mistake those for the separately allocated delivery
runtime or treat guessed "door" capture offsets as verified animation components.
No gameplay patch or installation was made for this investigation.

## Near-side pre-bridge combat, later unload and Bird diagnostic — included in 19:32 installation

Candidate DLL SHA256 `d7d296ef67e8defe3d0a30c65e73a68446fe414b4480d0096d6cd078addfef0b`.
Release linked; three Lua suites passed; diff check clean. Peak durable variables60.
Installer `/tmp/install-ember-prebridge-bird.py`, frozen file manifest
`/tmp/ember-prebridge-bird-manifest.json`. Game PID368720 still running at pre-install check;
user asked to close it. This was the earlier blocked installation check; these changes
are now included in the 19:32 experiment installation above.

- Move BOTH `BRIDGE_VIGNETTE_SUPPORT_A/B_SQUAD` into required Mercury combat. Their
  authored anchors (-420.5,79.5,-36.5) and (-420.5,73.5,-36.5) sit at the near end, not the far
  monster closets. The eight required squads now gate Ghost. Keep their original bridge
  AI objective14/task-group cost selection. Optional Crimson Shadow remains lever-only.
- Actual previous log showed entry completion261710/261845 and native delivery264787/
  264933: the existing three-second timer was running. Asked user whether to use bridge
  completion or add three seconds; no answer received before proceeding with the stated
  assumption of THREE ADDITIONAL seconds. Lua unload now waits3000+3000ms after entry.
  Four-second post-unload hover and immediate second-path retirement remain intact.
  If user clarifies the reference point, adjust this assumption; do not claim the old delay
  was absent or describe the new total as only3000ms.
- Bird at355562 is a native sobject creation failure: 16EE180 -> 170F190 -> 170B0F0.
  There were116 free network indices/571 allocations. The last bridge support-A receipt
  still had three alive at354175; no evidence that completed darkness release caused Bird.
  Snapshot archived at `build/first-encounter-audit/bridge-clear-bird-test-20260905.log`.
  The new diagnostic reads native failure reason at the log site before generic reason12
  replaces it, and counts the separate1024-record simulation bitmap. Original code maps
  reason11 to state heap,14 to simulation records,15 to authority records. Getter16CC5D0
  is called read-only behind an exact-byte build guard and SEH; bitmapRVA030B0340 verified
  against170B2B0 disassembly. No allocation/init hooks or speculative capacity fixes added.
  Bird is NOT fixed yet; next reproduction must capture this specific reason.

Ship-door answer: existing `transition` API accepts exact type23/device class80804F45.
The named Harvesters are type2 actors with embedded delivery/animation components and
have no named type23 door binding in this SDK. The lever call cannot be applied directly;
we still need a verified native actor/component animation command. No ship-door/warp
animation claim or guessed hash in this update.

## Lever animation, Harvester hover and bridge-clear scope — installed 2026-09-05 18:55

Lua changes tested against installed DLL
`13513b002ab38633560f16e59e6dcd2984165540830816c9bba5640cf8d4161b`.
Three Lua suites passed, diff check clean. Peak durable variables 60. No native code change.
Installed with `/tmp/install-ember-hover-and-lever.py` after confirming Destiny closed.
All seven installed script hashes verified against `/tmp/ember-hover-script-hashes.json`.
Backup: `build/hover-lever-darkness-backup-20260905-185535`. Runtime, settings and save
unchanged. No game launch or process stop. Manual validation of these changes is pending.

- Use the separate authored type-23 lever device (f6ffb59e slot7) with the existing accepted
  type-4 lever interaction. Initialize position0 with snap; move to position1 without snap
  on use. The object's interaction and the visual animation are distinct native components.
- Add a four-second departure hold after native unload completion and both cargo squads
  are observed. Empty C/D also hold. Preserve the three-second unload delay and immediate
  retirement at exit completion. Timers have durable scheduled/elapsed bits; duplicate
  receipts and module reload cannot restart the pause or repeat departure.
- Exclude helipad squads from bridge darkness completion. Their authored anchors are around
  (-305,-294,-24), ~450m from the bridge fight at y140..190. Bridge/passengers/Landing Sun
  remain required; a living passenger cannot be bypassed. Tests demonstrate last passenger
  death clears restrictions inside region64 while helipad is still active and populated.

User confirmed the previous build's lever interaction and improved despawn timing live.
Archived test log `build/first-encounter-audit/lever-hold-darkness-test-20260905.log` shows
bridge and Landing Sun cleared, but helipad retained 1+2+2 living actors. Passenger member
squad f6ffb59e/type1/slot26 also last reported alive1/consumed0 (t263045); its lingering
population is not explained by the helipad scope error. If the next test looks empty but
remains restricted, capture that actor's actual position/state before claiming all native
squads are clear. Do not silently remove it from completion requirements.

Harvester door/warp animations remain unresolved from the prior task. This change adds
hover time and the Mercury lever's authored animation; it does not invent ship animations.

## Lever progression and packet batching — installed 2026-09-05 18:30

DLL SHA256 `13513b002ab38633560f16e59e6dcd2984165540830816c9bba5640cf8d4161b`.
Backup `build/lever-and-packet-batch-backup-20260905-183010`. Installer checked the real
process namespace, confirmed Destiny closed and verified all eight installed file hashes.
Save retained; SDK, declarations and mission scripting enabled. User launches manually.

- Split the six required Mercury squads from five optional bonus squads (Crimson Shadow
  anchor and four bonus war-beast groups). Optional squads have their own durable encounter
  state. Route catch-up excludes them. Main combat alone enables Ghost; optional deaths
  cannot replay the controls directive or block the scan.
- The authored `LANDING_MERCURY_DOOR_BUTTON_OBJECT` now spawns at its package transform.
  New typed `set_interactable_object` Auth carries entry zero plus registered interaction
  child 80804FB8. Native F32220 requires that child before publishing reply 80804FB7;
  F33A90 -> F36640 sets runtime+720 after accepted use. Auth revision zero leaves control
  of that latch with native use, rather than forcing a false latch on every publication.
  Exact-object `on_event_object_interacted` starts optional squads and opens the door once.
  Removed the proximity-triggered door transition and bonus spawn.
- Sun/helipad requests run immediately in the completed Ghost callback, ahead of the ship
  start requests. Later bridge crossings do not respawn those waves.
- Transport now drains up to 16 complete received frames per service pass. Previously only
  one was consumed. Each response is flushed before another frame is consumed; partial input
  and blocked output stop the batch. This addresses a possible packet backlog, but a live
  timing improvement is NOT yet demonstrated. Lua still retires ships on second-path
  completion; no extra retirement timer exists. The accepted 3000ms unload delay is retained.

Evidence: archived latest log `build/first-encounter-audit/lever-delay-test-20260905.log`.
Actor callbacks had queued_ms=0, so prior derived-event batching alone did not explain the
remaining delays. Exit-start/retirement acknowledgements still lagged staged commands by
roughly 12–34 seconds. These logs do not isolate client application versus reporting latency.

Validation: release linked; all 12 portable CTest checks and three Lua suites passed; diff
check clean. New fixtures cover registered object Sense, duplicate use, stale generation,
partial packets, blocked response ordering, main combat completion without bonus combat,
wrong lever identity, proximity rejection, and no replay after reload/return.

Remaining: manual validation of the native lever and latency change. Harvester doors and
animated warp-out are NOT fixed in this build. Current retirement is native deletion. Actor
command kind 6 only edits a six-element behavior-tag set; kind 8 edits a transform; neither
has been established as a door/warp action. Harvester behavior imports map to 80B3B399 and
its component templates, extracted under the audit tags directory. No speculative parameter
hash or damage-based retirement mode has been installed. Capture native delivery/door
components during the user's next manually launched bridge encounter.

## Event dispatch and encounter accounting — installed 2026-09-05 17:46

DLL SHA256 `ee769dd010e9ab6a65b4f144e3f283ae88c823496520eba41d05919e7659b56c`.
Backup `build/encounter-accounting-backup-20260905-174651`; installer checked Destiny closed
in the real process namespace and verified eight installed hashes. Save retained; SDK,
declarations and mission scripting enabled. No game launch or stop.

Reviewed recording `/home/millie/2026-09-05 17-04-12.mp4`, including bridge and refinery
transition. Respawning Restricted banner appears after the console. Archived corresponding
log in `build/first-encounter-audit/bridge-darkness-test-20260905-1704.log`. All four
retire-generation receipts eventually arrive. Delay from staged retirement to logged receipt:
C/D about 33–34 seconds, A/B about 21–22 seconds. These timestamps alone do not distinguish
client application latency from delayed host observation. Do not describe them as proven
network latency. Native deletion still has no verified warp animation.

Changes:
- Derived script event dispatch now drains up to 64 ordered callbacks per service pass,
  stopping on any pending output or fault. Previously one callback per pass could accumulate
  a backlog under continuous squad-cost reports. Native input ordering and output commits
  remain serialized. Actor callback diagnostics now include queue age and remaining count;
  actor Sense diagnostics include the original receipt tick.
- Pending squad Auth is installed after the retained estate, even if the squad lease already
  installed it. Previously an older SDK objective body in the estate could overwrite the new
  pending body in that same packet, silently deferring its effective publication.
- Encounter zero-population completion also requires consumed counts for every authored
  member slot. Native squad Sense .11 (+612 count, +616 values) accounts for consumed
  requests: 4E8620 -> 4E8660 increments for unowned actor removal/death; 4E8200 increments
  failed delivery requests. These counts are not births. A temporary zero while requests
  remain must keep the wave outstanding instead of requesting zero counts and phase 2.
  Accounting-only Sense deltas now reach the script. Decoder selects exact nested schemas
  80807ECF/80809491, refusing partial or invalid lists rather than matching arbitrary widths.
- Existing darkness trigger/release policy, three-second unload delay, manifests and paths
  retained. No speculative door or warp opcode installed.

Latest test helipad slots 12/13/14 reached positive population at 171762–172019, zero at
174141–174701, and some repopulated afterward. Sun slots 56/57 similarly reached zero,
then slot 56 repopulated. Old Lua immediately retired each group at the first combined zero.
The guard fixes that premature cancellation when requests remain; the cause of initial
actor losses is still unproven. Logs now include consumed totals for diagnosis.

Validation: release linked; ten portable CTest checks and three Lua suites passed; diff check
clean. New regression checks cover a 1000-event burst, strict output barriers and ordering,
partial accounting rejection, temporary disappearance/repopulation and late accounting deltas.
Peak durable variables remains 54. Manual test of this exact installed build is outstanding.

Remaining: verify ship callbacks no longer lag, observe actual hangar deaths versus accounting,
confirm darkness clears only after combat; capture live delivery/door components during the
next user-launched run. Door animation and native animated warp-out remain unresolved.
Additional offline RE maps command kinds 0–9 through A98E40; kind 6 (A9C060) manipulates
actor behavior tags, kind 7 (A9BFA0) writes named scalar parameters via 576420. Neither is
verified to be a Harvester door/warp command. Do not guess parameter hashes.

## Bridge darkness policy and native ship cleanup — 2026-09-05

Installed at17:02; DLL SHA256 `cbdc7a6eec5edcc41f382dc76635931c34db99966bdc5073137fde2a74bd35bc`.
Installer confirmed game closed, backed up to `build/bridge-darkness-cleanup-backup-20260905-170239`,
and verified all eight installed runtime/script hashes. Save preserved, SDK/declarations and
mission scripting enabled. User launches manually; no game launch or process stop.
Latest user instruction: enable darkness only on completed Ghost interaction starting bridge
extension; release after bridge/passenger/Sun/helipad squads all clear. Earlier catwalk/pipe/
Mercury combat is excluded. Release outside landing; return to cleared area cannot reactivate.
Controller tests cover initial disable, stale/incomplete scans, duplicate scan, last remaining
hangar squad, completed return, reload and unfinished earlier fights not blocking release.

Native `set_darkness_zone` uses exact type35/class808099BD/schema808099BF. E4C110 reads
Auth .0; E4C0A0 additionally compares lifetime native+12 against current bubble. Roster
assembly derives policy from its final SDK Auth estate, pairs lifetime .5 with region/8 (64→8),
and clears participation+736 delay bypass while active. Wipe countdown remains inactive:
hardwipe .3=-1, nested .4 timer inactive. This is NOT verified full checkpoint/wipe recovery;
manual death/restriction/HUD testing is still required. No claim that this fixes Bird.

Live observation proved all four entry/exit revisions acknowledged. Each A/B had two native
cargo refs; C/D had none. All four outgoing disabled bodies reached native Auth, but actor
indices stayed live minutes later. Root cause: live Auth callback AB6E20 invokes AB71E0,
which retires only on generation CHANGE; AB7350's enabled=false only inhibits creation.
Previous AB78B0-based assumption described a different callback and was insufficient.
Retirement now advances generation1→2 at second path completion and sends empty delivery
manifest in the SAME update to avoid old cargo replay after the lifecycle reset. Native
mode0 uses AB89A0 detach and 56A8F0 destruction. Visual warp animation is not established;
this fixes the demonstrated missing cleanup, pending manual verification. Do not claim doors
fixed: native delivery completes without the user observing door opening. Mode1 was inspected
and routes through damage/death handling (3BD720/B43090/DDA5C0); do not assume it means warp.

Sun and helipad groups now start with bridge activation (five squads), once each; seven deck
defenders retain their crossing trigger. A/B unload delay remains3000ms. No ship timing change.
Native header/codec tests nine CTest passed; all three Lua suites passed. Release build passed.
Peak54 durable variables; native limit512. Generated Lua declarations now include deliver_squads.
Capture archive: build/first-encounter-audit/harvester-two-carriers-20260905-1623.log;
live snapshots ship-runtime-20260905-162329 through163042. Reviewed user's local recording
2026-09-05 16-23-18.mp4; ships visibly park after flying. No causal proof for random deaths yet.


## Three-second delay and two-carrier manifest installed — 2026-09-05 16:18

Installed DLL SHA256 `d240bb7f528a3f97885e861e0ba2cdf380efad85d4fd78398a18e59a6252a3ea`.
Backup `build/two-carriers-transport-backup-20260905-161841`; eight runtime/script hashes verified.
Installer confirmed Destiny closed; save preserved; SDK/declarations and mission scripting on.
User launches manually. No game launch or process stop.

- Delay now3000ms per ship after its first authored path completion (user requested +1s).
- A carries supportA+meleeA; B carries supportB+meleeB. C/D have no bridge passengers.
  This follows the user's two-on-bridge observation and the authored entry/anchor positions;
  exact original script pairing still reconstructed. All four retain bridge objective AI.
- Added bounded `deliver_squads` native manifest1..8 exact distinct same-registry squads.
  Two refs are one187-bit payload, preventing second-squad assignment replacing the first.
  Existing single-squad API remains supported. A/B wait for native delivery completion and
  positive population from both of their squads. C/D depart after their arrival timer without
  waiting for a nonexistent delivery. All retire once on second path completion.
- Native movement/doors/warp-out are NOT claimed fixed. Latest objective test still had
  one stationary ship and no acknowledged exit revision before Bird. Log archived as
  first-encounter-audit/harvester-objective-test-20260905-1548.log. Bird at208531 had free0,
  allocs430 and an allocator-exhausted line; unlike the prior free116 run, this IS index
  exhaustion. Keep both failure modes distinct.
- Exact transported ship bodies now log `ev=ember_transport stage=auth_staged slot=... bits=... body=...`
  for only the four Ember members. Next run compare outgoing exit revision2 with live Auth
  +256 and Sense +396. Use /tmp/ember-capture-door-details.py while ships remain alive; the
  linked-block base calculation now wraps subtraction to64bits (previous Python overflow).
  Prior parked snapshots152740..153138 showed authority TRUE for all four ships; do not
  assume missing entity authority without new evidence. Door boolean mapping previously
  traced via44E340 actually uses generic component enable/disable through590B10/594D70;
  it is not established as an open-door animation command.

Validation: Release built, eight CTest plus three Lua suites passed. Tests cover two refs
surviving exit-program replacement, duplicate/empty manifest rejection, 2999/3000ms boundary,
independent empty-ship exits, both passenger squads per carrier, transit/reload and no replay.

## Harvester objective membership and +2s unload installed — 2026-09-05 15:42

Release built successfully. Candidate DLL SHA256
`151a83a68a0d01b7ac66ac402cf73f782473f825b2ce4d109507395e3d986f27`.
Installed and SHA-256 verified all eight runtime/script files after the installer confirmed
Destiny was closed in the real process namespace. Backup:
`build/objective-delay-transport-backup-20260905-154242`. Save/account progress preserved;
SDK, Lua declarations and mission scripting enabled. User launches manually. No game launch.

User explicitly requested restoring the previous arrival-based unload, delayed roughly2s,
and assigning ships to objectives exactly as carried enemies are assigned. Implemented:
- All four empty ship parent squads assign EMBER_POWERHOUSE_BRIDGE_OBJECTIVE revision1,
  task group-1, then use the shared native task-cost selector across14 authored task groups.
  Parent counts remain0 so adding AI cannot create duplicate stationary ships.
- First entry revision1/cursor1 starts a durable2000ms timer per ship. Expiry requests native
  delivery once. No bridge Sense prerequisite. Duplicate arrivals cannot restart the timer;
  callbacks continue across region transit and script reload.
- Second authored path completion still retires each ship immediately and once.
- Removed the new generic device-event implementation and six-device completion gate.

Live test archived as first-encounter-audit/harvester-doors-test-20260905-1533.log. Snapshots
ship-runtime-20260905-152740 through153141 contain four live ship Auth/actor states. No delivery
was requested in this test: the discarded device helper compared schema table index with hash,
so it ignored native completion. All six device primary channels were0/sequence2 in capture
152920. B/D/C entry finished t226246/227389/228420; A did not report completion until298318.

User reports Bird when killing one of the bridge-start defenders, after waiting minutes for
ships that never unloaded. At t463844 native create failed with116 free entity indices and571
allocations. This favors initializer failure over exhausted index bitmap; do not assert it was
caused by failed ship removal. Door animation/objective hypothesis still needs a manual test.

Portable eight CTest and three Lua suites pass; controller validates actual timer_name field,
1999ms/2000ms boundary, independent late ship, reload/transit, objective cost selection, and
unchanged empty parent spawn requests. Peak53 variables, native capacity512.

## Bridge-ready gate and region-independent cleanup installed — 2026-09-05 15:21

User reports entry/drop working but a few seconds before bridge connection, doors remain shut,
ships fail to disappear, and Bird recurs. User clarified retirement must happen immediately
on completion of each ship's SECOND authored path. No timers or region-exit cleanup were added.

Installed DLL SHA-256 `d64edd4463d8d0a1ecad6d53ed16d5438bd423304c6af4a155e1ad2bedb9630f`.
Backup `build/bridge-ready-transport-backup-20260905-152123`; eight hashes verified.
User confirmed game closed, installer checked real /proc, save retained, SDK/declarations and
mission scripting enabled. No game launch or stop. User tests manually.

- New device_state event reports primary native Sense position and accepted sequence for type23.
  Sparse field updates are retained; invalid/nonfinite positions are rejected.
- Bridge initialization uses primary sequence1/retracted1, extension uses sequence2/extended0.
  Cargo waits for all six bridge devices to report sequence2 at position <=0.001 AND its own
  entry-path completion. Each event order is handled; duplicate receipts cannot redeliver.
- Actor-path and ongoing squad receipts continue after leaving region64. Previously the main
  controller discarded them outside that region. Exit revision2/cursor1 disables that exact
  native ship once, without waiting for player transit or another ship. Native AB8C00 routes
  disable through entity destruction (56A8F0) when the ship has authority.
- Bird evidence is now native network-entity creation, not proof of a ship retirement failure.
  `transport-doors-latest.log` captures 16EE180->170F190 via B428D0/3BD440/4D7110. Creation can
  fail in allocator1711D10 OR initializer170B0F0. Existing allocator exhaustion is now warn;
  each bounded retail failure stack also logs current free count and allocation count. No
  extra allocator hook, automatic retries, or pool-restock settings were introduced.

Validation: Release build passed; nine CTest cases and three Lua suites passed. New tests cover
sparse device receipts, stale generation, partially connected bridge, early and late arrivals,
refinery transit during unload/departure, exact second-path retirement and replay suppression.
Peak54 durable variables; native capacity512. Live visual behavior remains to be tested.

**Door animation remains unresolved and unchanged in this install.** Native delivery1029330
sets an authored boolean through44E340 and starts four effect bindings (+80/160/240/320 via
C7AFA0 -> 3D30F0/587970); these are not established door commands. The Harvester actor80FE22FC
binds delivery component template80C0E5D2, component instance ordinal15, to actor template
80FE22C9 ordinal5 through the boolean ref at actor-file0xB68. Destination definition offset1018
uses interface815B963A (80803A2B/80803A2A), implemented by actor component80803C23 feature6.
Extracted assets are under first-encounter-audit/tags. Need capture that actor boolean/animation
state during delivery, and entry/exit Auth plus authority while ships are alive. Do not invent
an open-door hash or replace the existing Auth body without preserving path and delivery.

## Endpoint and sequencing candidate installed — 2026-09-05 14:43

User confirmed that the prior build moved ships, but too late; troops fell to their deaths,
some squads spawned early, ships remained above the bridge, and Bird disconnected the run.
The prior candidate is not an accepted implementation.

Installed DLL SHA-256 `166c596f720094c2fa9d69e70bf29bd3495a0c1a20176c4c3ec7d87e487ed8d5`.
Backup `build/endpoint-transport-backup-20260905-144335`; all eight DLL/script hashes verified.
Installer checked the real process namespace and found the game closed. Save unchanged,
SDK/Lua declarations and mission scripting enabled. User launches manually; no game launch.

Concrete correction: the kind-3 path command used marker 0, the START of each authored curve.
Native AB0C30 indexes the curve's marker array; marker 1 is the far endpoint. Both entry and
exit now use marker 1. Read-only `sequence-runtime-20260905-143010` contains all eight source
and curve assets. `transport-curve-markers-20260905.json` summarizes them. A entry runs from
(-380,151,-2.5) to (-365,117,-22.5), approximately 20m lower. C entry's marker parameters are
[0,5], others [0,4]; these are spline parameters, NOT flight durations in seconds.

Cargo reservations and empty parent requests now prepare before Ghost is enabled, without
creating ships or ground passengers. Console completion extends the bridge and requests the
four entry flights without first queueing later ground waves. Bridge monitors arm at this
point; early bridge receipts cannot consume Lua progression. Ground bridge/Sun/helipad groups
enter on 25/75/100% crossings with existing forward catch-up and once-only guards. Actual
crossing behavior still needs a fresh manual test: earlier runs sometimes lacked receipts on
the initial traverse, and late ground placement has previously missed streaming readiness.

Video rechecked at half-second intervals from 3:10 through 3:34.5. Ghost deployed around3:15,
machinery moves during3:16–22, ships sweep into view3:24–29, and crossing combat follows.
Four reference contact sheets saved under first-encounter-audit/reference-arrival/drop-*.
The recording does not establish the full four-ship passenger manifest. Current one-squad-per-
ship pairing remains reconstructed; passenger A-family support/melee anchors cluster near A/B
entry endpoints, so original manifest deserves further verification before claiming full fidelity.

Bird remains unresolved. Archived `transport-bird-20260905-1419.log` contains first-session
failed sobject creation at t248202, after region transition240599–245393. Exit cursor receipts
completed230157–233507; those receipts alone cannot prove visible flight and can include native
timeout fallback. New bounded warning-level caller/stack traces preserve the native creation
failure boundary if Bird recurs, without globally enabling debug or suppressing the error.

Validation: Release build succeeded; eight CTest cases and three Lua suites passed. Packet
regressions reject the former start-marker payload. Controller covers deferred preparation,
scan-gated monitors/waves, arrival-only unload, independent exits, and no replay on backtracking
or script reload. Peak52 variables; runtime capacity512. Flight, safe unloading, departure,
full passenger composition and no Bird still require the user's fresh run.

## Auth replacement fix installed — 2026-09-05 13:23

User reports missing pre-bridge enemies; ships stationary; troops appeared in ships but fell
to their deaths; some enemies appeared on the bridge; far-side closets worked as expected.
No mission fault/capacity error in the archived `native-transport-live-test.log`.
The log contains zero actor_path events. Do not describe the prior candidate as successful.

Read-only captures establish the lost-state problem:
- `ship-runtime-20260905-130600` and the 130638..130657 series: all four named actors
  exist, enabled, initialized, generation1; Auth movement revision/seed/count are all zero,
  while delivery revision1 and the correct type1 squad reference are present.
- `encounter-runtime-20260905-130934` captures both F6FFB59E and D8CE8390, 69 rows,
  no read errors. Active ground squads such as F6FFB59E/33,34,36,56,57 have objectives
  and task groups but zero Auth requested-count array length and generation0.
- The earlier 130812 encounter capture has squad Auth read errors (wrong handle offset).
  Its component bytes exist, but use 130934 for Auth evidence. The native Auth storage
  handle is at component+368 for both types; squad's inline Auth copy starts at384.
- `actor-authority-20260905-130526` and 1307 captures preserve actor state; many actors
  no longer carry squad refs. Free actor slots retain stale bytes; count live first words only.

Native overrides replace complete Auth, not absent-root patches. Host now materializes
these bounded mission API patches against the last transported same-ClientRef estate.
Movement and delivery roots coexist; objective updates retain counts/profile/generation;
retirement explicitly replaces requested counts with zero or actor enabled with false.
Unsupported nested layouts are refused. Unrelated SDK APIs retain their existing behavior.
Squad lease storage now allows the full 1313-bit schema, including objective and counts.

The delivery routine begins unloading. Lua now reserves troops during flight and calls
`deliver_squad` only after entry progress completes, then waits for native delivery completion
and positive squad evidence before requesting exit. Packed transport state now uses FIVE bits
per ship; this candidate requires a fresh mission, not reload into the four-bit live state.

Release candidate built: `dce57b2cb894e3a08cc320f2657d6ed07bab08f41ed4c38db78fd4ea1614fcbd`.
Installed and SHA-256 verified eight runtime/script files after the user closed the game.
Backup: `build/auth-state-transport-backup-20260905-132343`. Save preserved, SDK and
mission scripting enabled, no launch. Installer: `/tmp/install-ember-auth-state.py`.
Validation: all eight portable tests and three Lua suites pass. New test covers sequential
movement/delivery/exit/retirement, repeated AI updates preserving spawn counts and mode,
zero-count retirement, malformed/short input, and maximum-category squad lease capacity.
Live visual movement and correct unload location remain unverified for this new candidate.

User's next correction, explicitly deferred until dropships are implemented: war-beast door
must use `LANDING_MERCURY_DOOR_BUTTON_OBJECT` (F6FFB59E/type4/19), not proximity.
Object activation/rendering, native interaction/destruction criteria, and object/device links
need auditing. Nearby devices are lever7, screen8, door9. No object behavior changed yet.
Current object Sense decoder refuses nonempty dynamic reply lists; this is part of that audit.

## Installed native transport and completed-area safeguards — 2026-09-05 13:01

Installed eight runtime/script files into the user's game, verified SHA-256 after copying.
DLL: `d688af8ed3b6b738689494d34b2e5e7b15c9be97a9f6b4839a126d8474394e43`.
Backup: `build/one-shot-transport-backup-20260905-130149`.
Save untouched; SDK and mission scripting enabled; image capture disabled. No game launch.
This supersedes the short-lived 12:52 candidate, which still had the host's old mode check.

**Root cause corrected:** type-2 Auth `.7` is passenger delivery, not a list of flight paths.
AB6600 dispatches its type-1 squad references to interface 80807DB4. On failure it calls
4E8270 on those squads, so sending type-58 references there was structurally wrong.
Movement now uses Auth `.6`, one kind-3 80807F7A action; A97BB0/4FFEC0 accepts type58
and resolves the authored curve. Movement completion uses nested Sense `.3`/80807F6E
progress and revision. Root Sense 5/6 now remain explicitly separate delivery fields.

Harvester actor asset 80FE22FC contains component 8080670A at 0x554C. Its registered
1029330 delivery implementation collects reserved requests (102A920), creates passengers
in vehicle seats (1022890), and completes after attachment handles clear (102DC80).
Passenger squads now use mode3, which 4E4580 excludes from ordinary ground placement.
Objective assignments and cost-driven changes preserve that mode. Codec, SDK preflight,
and host submission share the same accepted-mode predicate.

Bridge completion requests bridge, landing-sun and helipad defenders before crossing volumes
can trigger next-region streaming. It also reserves all four passenger squads and starts the
four named ships. Each ship departs independently after movement completion, native delivery
completion, and evidence its passenger squad existed. Current manifest is one squad per ship
in roster order; original passenger-to-ship pairing is **not established** by the shared names.
There is no timed ground-spawn fallback. Native completion can include cancellation/timeout;
these receipts still require visual confirmation and actor capture in the next user-run test.

Completed encounters now replace their native requested counts with zero exactly once.
Their durable phase prevents subsequent placements and AI reassignment. Advancing route
progress suppresses old, previously skipped triggers on backtracking; objectives/dialogue
cannot rewind. Successful ship exit (or a dead ship) sends a disabled member body: AB78B0
releases the actor and AB7350 prevents recreation when retained Auth is replayed.
These rules persist through region transit and a script reload in the same mission session.
A new Director mission starts a new session; save/checkpoint resume of all stages is not implemented.

Validation: Release build completed; seven portable CTest cases and all three Lua suites passed.
Production msg-6 fixtures distinguish delivery changes from actual action progress and reject
truncated input. Reservation and retirement encodings are checked independently. Controller
regressions cover independent departures, duplicates, clearing all 38 ground/passenger squads,
zero-count retirement, ship retirement, refinery transit/reload/return, and late early triggers.
Peak durable variables: 51 under the former 64 limit; installed runtime still supports 512.

**Next manual acceptance:** fresh mission, initial landing/door/console/bridge preserved; see
ships actually follow entry and exit curves, carry and unload every passenger, then see all
far-side groups before refinery. Kill passengers and backtrack after clear. Capture native
ship movement/delivery/Auth and far-side actors while powerhouse registry is still loaded.
Visible flight, full compositions/placement, and return behavior remain unverified in-game.

## Live 512-variable test — 2026-09-05

User manually launched and crossed into the refinery. No script/capacity fault in the
archived `build/first-encounter-audit/capacity-512-live-test.log`; structured squad/flight
summary is alongside it. User reports no visible post-bridge enemies. Do not treat server
spawn staging or positive Sense populations as proof of correct visible placement.

Cutscene completed at t=112840. Catwalk actor capture `actor-authority-20260905-120413`
confirms native objective D8CE8390/type3/index0, revision 1, assigned task groups, computed
costs, and AI area refs. Mercury groups cleared and Ghost completion started bridge/ships.
All four entry paths stop in 12–14 ms; exits stop in 13 ms. Read-only captures saved before
and after ship creation: `ship-runtime-20260905-120639`, `ship-runtime-20260905-120724`; full actor snapshot
at 120724 contains all four ships. Both 120820 and 120857 are empty after registry unload;
they do not contain member, Auth or entity evidence.

Bridge 25/75/100 triggers fired at t=369200/386691/390960 without fault; corresponding
spawn and objective commands staged. First positive populations for bridge slots 38/39
arrived at 399270/399470; 33/34/35 at 400133/399983/400746 (about 30 seconds late).
Slots 36/37 reported zero only. Need explain delayed population and actual placement,
and missing sun/helipad groups; capacity is no longer blocking callbacks. All four
passenger groups reached zero after previously positive counts without the old disconnect
before the user continued into refinery.

Native ship capture resolves all eight type58 path refs to class 80807D9B objects, excluding
missing registry mapping at capture time. All four ship members show generation/actor state
consistent with spawned ships, delivery revision 2, inactive, dead=false, initialized=true.
The later helper supports Auth/entity capture, but 120820 contains no rows. This does not
prove readiness at first dispatch. Those old revision/state fields described delivery, not movement.
No game files changed during this monitoring turn; no launch/shutdown or memory writes.

## Variable capacity update — 2026-09-05 12:00

User reports almost all enemy behavior now works, but stationary ships, missing passenger
and post-bridge enemies, then `mission variable capacity exceeded`. Run archived as
`build/first-encounter-audit/ai-working-flight-cancel-capacity.log`. The bridge 25% trigger
faulted while creating encounter state, preventing later mission callbacks.

Raised the shared C++ mission variable capacity from 64 to 512 at the user's request.
All variable-count fields are size_t; VM, transactional state, and SDK capacity publication
use the shared constant. Also packed per-squad AI assignments into 12 five-bit lanes per
integer. Full-route regression reproduces exhaustion with the previously installed scripts;
updated scripts peak at 57 variables under the original 64-slot test budget. A fresh mission
is required; an already faulted run is not migrated.

Release build, all seven portable tests, three Lua suites, and whitespace check passed.
Installed and SHA-256 verified eight DLL/script files, preserving save and enabling SDK and
mission scripting. Backup: `build/capacity-512-backup-20260905-120000`.
DLL SHA-256: `004fcd781af52bc8eaa3fc2e56be9ab447d0ef23335902fb0f576c2ac844eaf2`.
No game launch or shutdown.

Flights remain unresolved: all four entry paths report active then inactive within 13–15 ms;
exit paths also stop within 14–16 ms. Current transport interprets this transition as arrival,
but it is not evidence of a completed flight. All four passenger group slots reported positive
population, which does not establish complete composition or correct boarding/unloading.
Need a read-only capture of stationary ships during the next manual run to inspect path
resolution, actor readiness and component dispatch. No speculative flight change installed.

## Objective and flight test candidate — 2026-09-05

User confirmed bridge extension, then reported stationary ships spawning only on the return
bridge crossing and a world disconnect after killing passengers. The installed baseline had
no flight requests and used Bridge 25% as its spawn trigger. Candidate moves ship creation to
Ghost completion and starts all four named members on their authored entry sequences. Native
path-state receipts gate passenger creation, then exit sequences. Passenger attachment/unload
is still incomplete: this candidate creates passengers at their authored ground anchors after
arrival. Native flight/cancellation behavior requires the user's test.

Actor capture `build/first-encounter-audit/actor-authority-20260905-112423` contains 10 full
actors and five catwalk squad blocks. Actor and squad-owner authority bits are true. Squad
Auth root 0 is unset, so they are not assigned to an authored combat objective.

Derived objective assignment: 4E2A90 copies root 0 to squad +1520, root 13 to +1540,
and root 16 to +1532. 4EC710 links the squad into the selected objective group. AB8510 /
A99FA0 computes group costs for squads matching the objective's registry/ref; 4EB8E0 clamps
costs at 2040, and 4E84E0 echoes the completed calculation revision into Sense root 1.
AB83D0 / A9AD70 then selects native tasks within assigned groups; 4E2C40 applies the
combat-area reference to the squad AI group and refreshes its actors.

Candidate assigns all 38 ground/passenger squads to their corresponding same-registry
objective, initially group -1, revision 1. Retained native costs select the lowest reachable
authored group, preserving ties. This selection policy is reconstructed, not recovered retail
host code. Bounds from objective source +136: catwalk 17 (80B3CA03), Mercury 11 (80B3DCC1),
bridge 14 (80B3C8D6), pipe 3 (80B3DCC7), sun 4 (80B3DCC4), helipad 4 (80B3DDB2).
Cost-only Sense deltas retain population counts so AI updates cannot manufacture deaths.

Disconnect log archived as `build/first-encounter-audit/dropship-kill-disconnect.log`:
t=339052 four failed sobject creations, then prerequisite failure and world cleanup. No
verified fix yet. Existing lease-release diagnostic setting is unchanged: its applicability
to this run has not been established.

Validation: Release DLL build, all seven portable tests, three Lua suites, whitespace check.
DLL SHA-256: `b14c56923797150227f9b1e9b1feaa4b8198dde40c669a5ab65406624407bda6`.
Installed and SHA-256 verified at 11:44. Backup: `build/objective-flight-backup-20260905-114409`.
Save retained; SDK and mission scripting enabled. No game launch.

## Ghost completion decoder fix — 2026-09-05 10:45

User confirmed the beast door opens and the Ghost console is scannable, but the bridge
stays retracted. Archived that run as `build/first-encounter-audit/console-scannable-no-bridge.log`.
All eleven Mercury squads cleared and console enable Auth was staged at t=278017.
There were no decoded Ghost Sense receipts. The native msg-6 decoder lacked the
80804D3E branch, so the new Lua completion handler could never receive its reports.

Added the exact required boolean, raw float and biased signed generation fields from
SDK schema 80804D3E and native E4A590. Independent complete packet fixtures now run
through the production decoder and Ghost state reader: initial, active half-progress,
and completed generation 2; a truncated packet is rejected. The test failed with the
old decoder's partial status and passes with the fix. Both Lua suites and all six
portable tests pass; Release DLL build and whitespace checks pass.

Installed and hash-verified DLL/scripts with backup
`build/ghost-decoder-backup-20260905-104521`. DLL SHA-256:
`26611bdea79a39305d2fcda7a64ec06627c5a07ec9af8ee64e009d393cb42dc3`.
No save edits or game launch. Fresh manual run still needed to confirm that completing
the native scan triggers all six bridge animations. AI and transport work remains open.

## Installed bridge, door and console test — 2026-09-05 10:34

User confirmed the opening cinematic and player arrival work. Preserve that path.
Installed Release DLL and five Lua files, with source/installed SHA-256 verification.
Backup: `build/bridge-console-backup-20260905-103443`.
DLL: `eed579d58c7131090bb87d6b7521b9ef8063f4c5a95f70ff68dc3a0a88bf83c9`.
Save files were not modified. SDK and mission scripting remain enabled. Native image
capture is now disabled because the complete image has been archived. No game launch.

This candidate initializes all six bridge devices to position 1 (retracted inferred from
native value passthrough and the user's observation that position 0 was extended), opens
the beast door at the authored Mercury 40% trigger, and enables the native Ghost console
only after all eleven Mercury squads report real populations and then clear. The squad
state flag previously called removal_flag is not a reliable unload indicator: observed
real kills set it, and it no longer suppresses death accounting. Exact type-65 Auth and
retained partial Sense fields connect console completion to animated bridge extension.

Validation: Release build current, git diff whitespace check clean, both Lua suites pass,
and all six portable CTest tests pass. These are implementation checks; bridge direction,
door timing and native console completion still require a fresh manual mission test.
AI movement/idle and carrier/passenger attachment remain unresolved and are not fixed
by this candidate. In the prior run, Mercury bonus-anchor squad slot 42 remained alive.

Transport investigation checkpoint: type-2 Auth root 7 carries an authored sequence list
and revision; native AB6600 resolves it and starts actor sequences, with Sense revision
and active fields at offsets 448/452. This is distinct from loose squad creation or
adopting an existing actor. No transport change has been installed. Squad Auth root 0
is not proven to reference a dropship; its resolved target expects a controller layout,
so do not implement a carrier link through that field on this evidence.

## Phase-zero sandbox fault — 2026-09-05

The manual run failed before cinematic activation. Installed log at t=52632 reports
`landing.lua:52: attempt to call a nil value (global 'pairs')`. Sunrise deliberately removes
unordered iteration from its mission sandbox; the standalone Lua test had left it available.
Both controller uses now iterate an explicit route-ordered array with `ipairs`, including the
squad-state callback. The regression disables `pairs` and `next` before loading the controller:
it reproduced the exact old fault and passes the full route/reload test with this fix.
No native DLL change or save reset is required. Start a fresh mission instance after installation.

The user's manual run also produced a complete native image dump (zero unreadable bytes).
It and the fault log are preserved under `build/first-encounter-audit` for offline investigation.

## Powerhouse route test build — 2026-09-05

Correction to the earlier spawn investigation: `powerhouse_landing_mercury` is NOT the mission
start. It is the later console combat platform. The authored `default` set (`0x2EA8FB98`) has
an opening three-point cluster around (-400, -192, -1.9), surrounded by the
`powerhouse_catwalk_002_dialog_trigger_volume`; the catwalk progress volumes then lead north.
That is the new selected set. Actual native spawn selection still needs the user's fresh run.

The Lua route now binds all 42 powerhouse squads once in nine groups. The first five are the
catwalk entry squads; 15%/35%/100% catwalk crossings stage mid-catwalk, pipe and Mercury.
Pipe 40% starts cue 2. Bridge 25% stages bridge/dropship/passenger groups; bridge 75% stages
landing-sun, and bridge 100% stages helipad. Later crossings catch up skipped narrow volumes.
These choices are reconstructed policy using authored spatial identities and the reference
video, not a claim that the packages contain recovered retail host conditions.

Fixed a native Sense reader bug: a presence-gated alive-count field exists as a DecodedValue
row even when omitted. Checking only the row pointer interpreted that omission as zero.
The reader now requires a present root count. Added squad population receipts, exact resolved
player-trigger identities, and bounded change-only bridge/console Sense diagnostics.

Validation: two Lua suites pass (cinematic ordering, all 42 roster entries, route progression,
exact-trigger matching, skipped volumes, removal, repeat/reload guards); all five portable
CTest tests pass, including omitted/zero/nested/invalid alive-count cases; six SDK inspector
tests pass. All 42 squad bindings and seven route triggers resolve against the generated SDK.
These checks do not prove live actor creation, native AI, Ghost interaction or bridge movement.

MissionDocs were reviewed. The Forest race switch is specific to Omega/Vex and is not suitable
for Ember/Cabal. The documented member-bound actor path needs native verification before
applying it to harvesters and other named Ember members; loose squad requests alone do not
establish their correct creation/attachment. Ghost-console Auth semantics and bridge direction
also remain unresolved. No automatic console completion or bridge opening was added.

User constraint: apply and save to the installed game, but NEVER launch it automatically.
The next manual boot will use Sunrise's existing native-image diagnostic to supply readable
code for offline analysis; disable the diagnostic once captured. Existing save/settings,
scripts and DLL are backed up before installation. SDK and mission scripting remain enabled.

Installed and SHA-256 verified the Release DLL and all five Ember Lua files.
Save/account state compared equal before and after this installation. Backup:
`build/powerhouse-route-backup-20260905-090720`. DLL SHA-256:
`26edd28c539beb6444c6181684154c1e43abb8940cf26c3d5eec87cfd54bcfac`.
The installed diagnostic flag is enabled for the next MANUAL boot. No game launch was made
after the user's manual-launch-only instruction. Next validation: fresh Director launch,
cinematic completion, grounded starting platform, catwalk/pipe population and AI, then console.

## Earlier landing spawn attempt — superseded

The next playtest confirmed the movie completes and the player spawns, but in the wrong place
under the map. Logs now resolve both cinematic notifications and arm the transition to state 64.
The installed arrival override selected the cinematic region without an explicit spawn set.

The native build-data catalogue contains a three-point landing set whose hash matches
`powerhouse_landing_mercury` (`0x9C58857A`). Its points align with later Mercury anchors. The claim that this was the opening area
was incorrect; see the correction above. Settings now explicitly select that set while retaining
bubble 6/state 49 for the movie; Lua still selects playable state 64 after termination.

The spawn-binding filter previously discarded explicit sets when their activity package was
absent from the scenario's direct package list. That list does not inventory transitive
references. Explicit overrides now retain their documented precedence; inferred spawn sets
keep the existing package filter. Both roster and global-state publishers use this helper.

Validation: all four portable CTest tests passed, including explicit override precedence,
absent/zero overrides and the existing inferred-set filtering behavior. The Release DLL built
successfully and the native settings probe accepted the updated configuration with SDK and
mission scripting enabled. Installed and verified the root DLL and arrival setting, preserving
other preferences. Backup: `build/landing-spawn-backup-20260905-081125`.
The selected landing position still requires a fresh in-game test after the cinematic.

## Cinematic handoff fix — 2026-09-05

Playtest confirmed the arrival movie plays, but the player remained black after fade-out.
Both native notifications arrived (targets 5239 and 1685); the mission runtime rejected each
as `cinematic absent`. Inspection of the retained records showed zero-filled payloads and
ClientRefs decoded as registry 0, type -1, slot -32768.

Root cause: `incident::validate` advanced past selector, optional words and payload without
retaining their contents. It now reads and stores those fields through the bit reader,
including unaligned payloads. This restores the source identity needed to deliver the exact
cinematic-termination event to Lua and select landing state 64. Player-trigger events used
the same discarded payload path. Cinematic resolution now logs the source identity and
successful start/termination for the next playtest.

Validation: the new regression fails against the original parser and passes with the fix.
It covers every payload alignment, maximum selector/payload lengths, truncated frames,
optional fields, cinematic encode/parse/decode/catalogue resolution and player-trigger decode.
All three portable CTest tests and the Lua handoff test pass; Release build passed.

Installed and SHA-256 verified the rebuilt root DLL. Previous DLL is in
`build/cinematic-handoff-backup-20260905-075750`. No SDK or settings regeneration was needed.
The restored notification path still needs the next in-game spawn test.

## Current work — 2026-09-05

The sections below this update record earlier playtest builds. Current investigation found
that the offline SDK generator omitted the scenario-layout catalogue needed by container
and spatial extraction. Initializing it and rebuilding the Ember shard recovered 83 additional
squad definitions: 160 definitions, 138 runnable, across 162 squad sensors. Thirteen sensors
still have no definition, and eleven processing sensors each have two refused alternatives.
See [the complete squad census](MISSION_EMBER_SQUADS.md) and
[the timestamped video comparison](MISSION_EMBER_REFERENCE.md).

The production generator now prepares the missing catalogue in offline mode, waits for it
in game mode, and rejects cached shards that lack placement context for a known map stem.

The controller draft uses the separate arrival-cinematic state (49), waits for its exact
termination event, then selects playable state 64. Bridge reset is delayed until region 64
is held. Region-less client deltas preserve encounter ownership. Arrival dialogue waits
for spawn settlement; the console-guidance cue follows combat clear. Regression tests pass,
but these changes have not yet been validated visually in game.

The Ghost console interaction, later encounter activation, later dialogue and full mission
completion are still unfinished. The reference video begins after arrival and cannot verify
the identity of the arrival movie. No claim of complete 1AU parity is made.

Validation: Release build, both Lua controller/encounter suites, six Python SDK checks and
eight sanitized production-linker regressions pass. A fresh native offline process loaded
the scenario catalogue itself and rebuilt the old cache successfully into `build/sdk-corrected`
(138 runnable Ember definitions). The script binds successfully to that SDK, including the
newly recovered eleventh landing squad. The runtime logs typed Ghost-link Sense fields for
the next console playtest without interpreting them as an interaction yet.

Installation completed after the game closed. The previous runtime, SDK, scripts and settings
are backed up in `build/video-audit-backup-20260905-074152`. All installed file hashes matched
the prepared files. Settings now select bubble 6/state 49 for cinematic arrival. A follow-up
Lua gate additionally rejects early playable-region reports before cinematic completion.

Millie supplied a separate 21-second arrival cutscene reference and specified ordinary fly-in,
then the cutscene, then player spawn. The reference document records that evidence. The selected
bookend asset still needs visual confirmation during play.

Workspace recovery: the parent checkout was moved to Trash during the audit. The mission
folder now has its own recovered `.git` directory, retaining branch `mission-ember` and all
prior commits. Compiler dependencies were copied into `build/recovered-dependencies/xwin`;
the local `.xwin-cache` points there. The trashed checkout was not modified.



## First test build

The opening landing controller is implemented and loads with the regenerated SDK. It publishes
the powerhouse state only when the client holds region 64, places the six landing squads and
changes the directive after all six have been observed and cleared. It leaves bridge movement
for the authored interaction. This is a development slice, not a complete implementation of 1AU.

Build: `build/x64/Release/steam_api64.dll`.
Regenerated SDK: `build/sdk-generated`.
Prepared installation overlay: `build/playtest`.

The overlay was installed into the game's `bin/x64` directory on 2026-09-04. It contains the
rebuilt root DLL, generated SDK and opening scripts. The previous files are backed up in
`build/installed-backup-20260904-224413`, with an `installation.json` listing replaced paths.
The scripting switch `server.activation.mission_scripting` was already enabled; no settings
were changed. DLL, SDK-pack and entry-controller copies were checked against the prepared files.

## Fixed extraction problem

The squad linker resolved object keys across the entire content estate. Ember and its arcade
variant reuse object keys, so a spawn-rule slot in the arcade object could invalidate an exact
campaign reference. The linker now restricts target descriptors to objects sharing an authored
scenario with the source. Ambiguity inside a shared scenario remains a refusal.

Package evidence: decoded all 162 Ember spawner configs using the existing package reader and
installed Oodle codec in an isolated Wine prefix. Their 173 valid raw references all resolve to
unique slots inside Ember; 116 collide in the global slot lookup. This census establishes the
scope problem; it does not establish that every spawner passes every subsequent runtime gate.

The production offline generator was run with the rebuilt code into an isolated output tree,
reusing 468 matching scenario shards and publishing 1,643 Lua files. The verified output was then
installed for the opening playtest, retaining the originals in the backup above.

| Ember resource | Installed SDK | Regenerated SDK |
| --- | ---: | ---: |
| Squad sensors | 162 | 162 |
| Squad definitions | 31 | 77 |
| Definitions passing static placement checks | 29 | 64 |
| Sensors without a squad definition | 131 | 87 |

The six opening squads all have exact actor bindings, anchors and passing definition flags.
The remaining missing definitions and refused rows require further work; the scope fix is not
a claim that all mission combat is available. Static placement checks cover definition flags,
member counts and the default actor-profile compatibility gate. Live lease, transport and AI
behavior still require in-game validation.

## Region map

Ownership is taken from SDK state/occurrence joins. This table is not a complete event-order map.

| Region | Authored name | Relevant content | Squad sensors | Passing static placement checks |
| --- | --- | --- | ---: | ---: |
| 64 | powerhouse | Landing, bridge, catwalk, helipad | 42 | 28 |
| 56 | link | Processing encounter and machinery | 15 | 2 |
| 40 | cinder | Foundry, chamber, grinder, ascent, sunlit deck | 49 | 31 |
| 0 | apex | Interceptor/security, reactor targets, fusion-cell area and escape | 56 | 3 |
| 1, 2 | apex alternatives | Alternate authored states | 0 | 0 |
| 48, 49 | unnamed globals | Shared mission sensors and bookends | 0 | 0 |

The generated-world shard's authored-squad context vectors are empty. Its projection helper
`append_scenario_graph_contexts` has no caller in the current generation path. The opening
controller uses the runtime pack's squad definitions, so wiring that separate graph projection
was not included in the scope fix.

## Verification

- Full Release Windows DLL build passed using clang-cl and the existing Windows SDK.
- CMake now includes the embedded Lua sources/headers and enables C++ exceptions, matching the
  Visual Studio project's requirements.
- Eight production-resolver regressions passed with address and undefined-behavior sanitizers.
  Leak detection was disabled because the execution sandbox uses tracing.
- Six SDK-inspector validation tests passed, including corrupted/truncated inputs and wrong
  scenario identity.
- Lua encounter and controller checks passed for initial empty reports, duplicate events,
  removals, explicit reset, region transit, region ownership and controller reload.
- The opening controller rejects the old SDK's missing squad binding and loads with the newly
  generated SDK. No in-game playthrough has been performed.

## Next checkpoint

Launch campaign 1AU and check that the landing enemies appear,
fight and allow the directive to reach the bridge controls. Check death and re-entry behavior
without expecting complete authored checkpoint support yet. Keep the mission-script/runtime
log from this run so spawn or lease refusals can be distinguished from missing AI behavior.

Then verify the bridge's interaction event and extend the controller through the next encounter.
The later mission, full checkpoint/wipe handling, dialogue, cinematics, rewards and terminal
completion remain unimplemented. Do not mark the mission complete on the basis of this test build.

## Live-test correction: VM arena allocation

The first opening-area test reported `mission arena block could not be taken from the heap`
before any events. An isolated Windows executable linked against the Release objects reproduced
it: arena initialization returned false with capacity zero. LLVM IR showed that the nothrow
byte-array new-expression had been reduced to the failure branch, with no allocation call.
An explicit `::operator new[]` call preserves the allocation; the 64 MiB limit is unchanged.
The rebuilt production allocator passed allocation, growth with data preservation, capacity
refusal, free/coalescing and three release/reopen cycles under Wine. Regression source:
`tests/mission_arena_test.cpp` (run with Release optimization; checks do not rely on assert).
The corrected DLL is installed; backup: `build/dll-before-arena-fix-20260904-230158.dll`.
A fresh in-game launch is still required to verify the next mission startup stage.

## Upstream sync — 2026-09-05

Merged official Sunrise master at `a57dc9a` into `mission-ember`, preserving the mission
scripts, arrival override, scenario-scoped squad linker and Windows arena allocation fix.
Upstream now supplies the equivalent Lua CMake integration, so its CMake version replaces
our overlapping build edits. The native SDK format and mission APIs are unchanged; the
existing generated SDK is retained. Release build, Ember Lua checks, SDK inspector tests
and upstream portable tests pass. This sync does not expand mission coverage or resolve
remaining bridge-interaction, encounter-timing or missing-definition work.
