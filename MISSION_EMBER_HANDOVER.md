# 1AU / mission_ember — implementation and engineering handover

Prepared 6 September 2026 for Millie and the next agent working on Sunrise. Read this document before the older plans or transcripts. The delivered ZIPs preserve **`b9880dc`**, whose gameplay implementation was `88a9632`. This working handover now also documents the subsequent gameplay-HUD correction; the existing delivery archives have not been replaced. Later historical notes often describe approaches that have already been replaced.

## 1. Current result and first action

1AU is scripted from the opening cinematic through the escape and two ending movies. The user has repeatedly played the route and described it as almost complete. The latest investigation isolated the ending presentation problem after resolving native playback, texture residency, movie sequencing and return to orbit.

**The user confirmed the installed HUD correction (`3fea388`) works perfectly.** Preserve this tested baseline. The following describes the preceding failure and its correction. The user tested `88a9632` and reported reticle, ammunition, abilities, radar and mission objectives still rendering over the movie. The log confirmed `requested=43 selected=33 applied=33` at t=766752 and STM playing at t=767072. Native HUD ownership is separate from the cinematic overlay window; see section 17. Do not start by rewriting mission progression or the movie loader.

The earlier build, `41e2728`, played both movies through the native decoder. Movie one was skipped with Escape; movie two reached its natural end. The game accepted completion, selected orbit and reached native `orbit_setup`. However, a loading window covered the video. `88a9632` changed the selected UI state from `0x22` (`loading`) to `0x21` (`cinematic_overlay`). That mapping is verified through the native state-to-window tables, rather than inferred from the broad cinematic category. The subsequent HUD filter keeps that correction and excludes only the native role-18 HUD window's drawing during owned movie presentation. It is built, ABI-verified and installed. The first installation attempt stopped before any game writes because Destiny was running; installation succeeded after a subsequent process check confirmed the game had closed.

Installed HUD-correction DLL SHA-256: `c2e84a350012ffd11eaeb39ef39bcb385b986c42dac5c0386ac0d0325f7fcd5b`. The user confirmed the HUD fix works in game; no separate menu/skip test matrix was reported. The guarded installer verified the DLL and all 17 mission scripts against their source hashes.

Latest installed DLL SHA-256:

```text
c2e84a350012ffd11eaeb39ef39bcb385b986c42dac5c0386ac0d0325f7fcd5b
```

Installation manifest: `build/ember-installation.json`. Last backup: `build/ember-install-backup-20260906-212949-077349`. The old `build/apex-cooling-installation.json` still records the pre-filter delivery baseline. The install replaced the DLL and 17 mission Lua files, verified their hashes, and preserved settings, save and generated SDK. The agent did not launch or stop the game.

### What the evidence establishes

| Area | Evidence and current confidence |
| --- | --- |
| Original opening, landing spawn, first encounter, bridge, lever, Harvester doors/exit | User confirmed these across earlier tests. Preserve the regression baseline. |
| Full route through refinery, Sunside, Foundry and reactor | Repeated user playthroughs reached the escape. Automated route tests exercise all phases; this is not a claim of every authored detail matching retail. |
| Movie picture | User explicitly confirmed visible first-movie picture on `15e507e`. Subsequent captures show valid video surfaces for both movies. |
| Movie sequencing | `41e2728` produced first completion, second request, second playing and second natural completion. |
| Orbit handoff | `41e2728` accepted completion, queued orbit, requested native cleanup and reached orbit setup. |
| Gameplay HUD, subtitles and menus | `88a9632` selects state 33, but the user reports gameplay HUD over the movie. The user confirmed the installed role-18 HUD fix (`3fea388`) works perfectly. Individual subtitle/menu/skip combinations were not separately reported. |
| Remaining finale polish | Beam sound/screen effect, escape scorch, explosions, extraction-ship path and menu/skip behavior need targeted confirmation. Earlier source changes are not proof of visual or audio success. |
| Fireteam wipe and respawn presentation | Implemented and covered by tests; a multiplayer acceptance run is still valuable. Do not imply solo observations prove co-op correctness. |

## 2. User instructions that must survive the handover

- The user launches and closes the game manually. **Do not launch or stop it.** Do not launch through Wine. If a standalone package reader needs a Windows runtime, use the Proton script; that is a separate process from the game.
- Installation is authorized, but first verify the game is closed, back up the files and verify the installed bytes. Keep the existing save, settings and SDK unless a specific change is needed and explained.
- Commit work without a co-author tag. Do not push to a remote without a request.
- Provide concise, meaningful progress updates at least once per minute during active work. Silent reverse engineering caused significant frustration.
- Do not describe passing tests, an accepted request, or audible movie playback as proof of rendered presentation. Report the specific receipt or observation.
- Completed areas are one-shot on backtracking. Only a confirmed checkpoint wipe resets the relevant encounter.
- Darkness starts after the bridge scan/activation and clears after the relevant area enemies die. Outside darkness, respawn is 3 seconds. In darkness, a dead player waits 30 seconds while someone is alive; when all known fireteam members are dead, show a 3-second wipe countdown and restart the checkpoint.
- The user explicitly withdrew the tube-entry death issue after identifying a noclip interaction. Do not reopen that work without a new report.
- The user asked for subtitles and normal inventory/settings access during ending movies. The earlier broad HUD draw suppression was removed. Investigate native UI behavior before reintroducing a mask.

## 3. Workspace and artifact map

| Item | Current location |
| --- | --- |
| Repository / branch | `/home/millie/Documents/Sunrise-builds/mission-ember`, branch `mission-ember` |
| Origin | `https://github.com/Nyxaraa/Sunrise-Nyxara.git` |
| Installed game directory | `/home/millie/Games/Sunrise/bin/x64` |
| Game packages | `/home/millie/Games/Sunrise/packages` |
| Runtime DLL | `build/x64/Release/steam_api64.dll` → game `steam_api64.dll` |
| Mission entry script | `scripts/mission_ember.lua` → game `Sunrise/scripts/mission_ember.lua` |
| Mission modules | `scripts/mission_ember/*.lua` → game `Sunrise/scripts/mission_ember/` |
| Installed SDK pack | game `Sunrise/activity_sdk.pack` |
| Installed generated SDK | game `Sunrise/sdk/` |
| Log | game `Sunrise/logs/sunrise.log` |
| Current native-image reference | `build/first-encounter-audit/game_image.bin` |
| Latest playback capture | `build/first-encounter-audit/41e2728-live-20260906-205738/` |
| Latest UI capture | `build/first-encounter-audit/41e2728-ui-windows/` |
| External contributor docs | `/home/millie/Documents/Sunrise-docs/MissionDocs/` |

The delivery contains two ZIPs. The branch ZIP has the complete committed source tree and a Git bundle of this branch's reachable history. It also carries the current DLL, installed generated SDK, selected diagnostic evidence and a manifest. The scripts ZIP contains the 17 mission Lua files in their `Sunrise/scripts/` deployment layout. The branch ZIP's `_snapshot/` directory describes included runtime assets and checksums. It is supplemental archive content, not part of the source commit.

The 59 GB ignored `build/` cache is not copied wholesale. Selected evidence is included under its original relative `build/first-encounter-audit/` location so the native verifier can run. Compiler caches, old DLL backups, full gameplay recordings and the installed game packages are not needed to represent the Git branch and are not in the delivery. The manifests record what is actually included.

### References

- Gameplay: <https://www.youtube.com/watch?v=PqurUhqC2CE>. Earlier work focused on the finale at 21:20–28:30 and the beam surge around 23:38–23:44.
- Opening movie: <https://www.youtube.com/watch?v=IUmUYHELJAY>.
- Mission scripting reference: <https://github.com/stanuwu/SunriseMissions>.
- Contributor guide: `IKORA-ANIMATION-AND-ENDING.md` in MissionDocs.
- Enemy reference: `omega-forest-enemies.md` in the user's documentation.

These links are the user-supplied references, not newly reviewed video evidence during preparation of this document. Existing reference notes and captures should be consulted before changing encounter cadence.

## 4. Read order and historical traps

Read these first:

1. This handover.
2. `docs/mission-ember-prerendered-ending.md`.
3. The newest sections of `docs/mission-ember-final-corrections.md`.
4. The relevant current Lua module and native implementation.
5. The test covering that behavior.

The following are useful history, **not current implementation authority**:

- `MISSION_EMBER_STATUS.md`: many chronological entries with obsolete installed hashes and failed approaches.
- `MISSION_EMBER_FINAL_ISSUES_PLAN.md`: earlier plan based on `50e68b7`; its “do not implement” instruction applied to that planning turn, not the current task.
- `OpusHandoff.md`: earlier contributor transcript, preserved unchanged.
- `MISSION_EMBER_POST_OPUS.md`, `MISSION_EMBER_REACTOR.md`, `MISSION_EMBER_SQUADS.md`, `MISSION_EMBER_PLAN.md`, `MISSION_EMBER_REFERENCE.md`.
- `docs/mission-ember-apex-cooling-and-scorch.md`: mixes useful authored mappings with superseded world-transition, hazard and timing experiments.

Examples of obsolete claims to avoid carrying forward: ending movies must select regions 1/2; broad HUD suppression is still active; movie completion waits for texture eviction; UI `0x22` is ready cinematic presentation; the beam still has an independent pre-roll timer; escape uses only the placed damage object; Electron Controllers still use fixed access-objective tasks. Read current source for each.

## 5. Mission identity and generated SDK

- Mission scenario tag: `80B3C09E`.
- Activity definition: `38F926B2`.
- Generated module: `sdk/lua/missions/mission_ember_80b3c09e.lua`.
- Root script resolves it through `require('missions').MISSION_EMBER`.
- Installed Lua manifest content key: `sha256:dbd228327d14e7b42ea920e7e3da55b3314b9d5483b0f31bb6ac73afc92dcc35`.

The SDK pack, generated scenario shards, catalog and Lua declarations form a matching set. The source tree alone does not contain the full generated SDK. The archive supplies the installed set; game packages still come from the user's installation. Do not mix a stale pack with a newly generated Lua tree.

The authored mission is split into packed region indices:

| Region | Role |
| --- | --- |
| 49 | Opening cinematic alternate; initial state `STATE_80B3C09E_0006_0001_80B3C09A` |
| 64 | Powerhouse / landing / bridge |
| 56 | Link / Processing / refinery |
| 40 | Cinder / Sunside / Foundry |
| 0 | Apex / access / reactor / escape |
| 1 and 2 | Authored ending alternatives; current direct movie path deliberately does not travel there |
| 48 | Shared/global state; do not confuse it with the held opening alternate 49 |

Binding must be exact: registry key, slot type, slot index and, where relevant, generation/revision. A requested destination is not an arrival receipt. A slot name existing in Lua does not prove its native object is present or its authority has been applied.

## 6. Lua architecture and runtime contracts

`mission_ember.lua` is the entry point. It constructs opening, landing and wipe controllers, then lazily constructs the later route. Events are delivered through explicit callbacks for client state, cinematic incidents, player triggers, object interaction/state, squad state, actor path state, damage state, timer completion and fireteam state.

| Module | Responsibility |
| --- | --- |
| `opening.lua` | Start intro only on held region 49; exact completion/skip source; transition to landing once |
| `landing.lua` | Landing encounters, lever/Crimson Shadow, bridge console, darkness and navigation |
| `roster.lua` | Opening encounter squad lists |
| `encounter.lua` | Reusable durable encounter state and one-shot spawn/clear/reset |
| `combat_ai.lua` | Native authored objective assignment and cost-based task-group selection |
| `transport.lua` | Four Harvesters, reserved cargo, authored paths, delivery and exit action |
| `routes.lua` | Region dispatch, forward progression ownership, ending event gate and checkpoint reset routing |
| `route_support.lua` | Exact slot matching, later encounters, devices, objects, effects, directives, cues, checkpoints |
| `route_roster.lua` | Later encounter squad/objective mappings |
| `carry.lua` | Native cell pickup/deposit, ownership tracking, consumption and expiry recovery |
| `processing.lua` | Refinery machinery, fusion-cell objective, waves and exit hatch |
| `cinder.lua` | Sunside, retreats, indoor checkpoints, grinder/ascent and Foundry |
| `apex.lua` | Controllers, reactor cooling, clamshell/core targets, final cell, hazards, explosions and escape |
| `music.lua` | Authored music section selection at mission milestones |
| `ending.lua` | Direct movie 1 → movie 2 → lifetime completion, polled at 250 ms |
| `wipe.lua` | All-dead countdown and native checkpoint-restart handshake |

Progress is stored in the transactional state exposed by `state:variable` and `context:set_variable`. Module-local tables are declarations and controller objects, not durable mission progress. Event callbacks stage typed intents; accepted Lua intent construction is not the same as native application.

Current bounds are important:

| Bound | Value / source |
| --- | --- |
| Durable variables | 512, `state/activity/mission/definition.h` |
| Timers | 32 |
| Intent burst | 63 |
| State/timer key storage | 64 bytes including terminator |
| Durable string storage | 128 bytes including terminator |
| Lua arena | 64 MiB, `mission_script_vm.h` |
| VM wrapper storage | 512 KiB |
| Latest full-route mock peak | 244 variables, 61 intents/event, 3 timers, 13,000 instrumented instructions/event |

The script sandbox excludes some standard string functions. A prior production fault before Foundry was caused by `string.find`; the route harness now removes those functions during callbacks. Prefer exact names and explicit tables to pattern parsing in mission callbacks.

A callback budget is not an invitation to schedule everything in one event: several setup/reset actions already approach the 63-intent ceiling. Split independent setup stages with short generation-qualified timers when needed.

## 7. Enemy spawning and native AI

The decisive AI discovery was that spawning enemies is not enough. Squads must belong to the correct authored combat objective. That objective supplies combat areas and task behavior. This resolved the “shooting without moving/no idle behavior” class of failure.

The common pattern is:

```lua
context:squad(mission.Squad.SOME_SQUAD):place{
    mode = context.sdk.squad_modes.replace
}
context:slot(mission.Slot.SOME_SQUAD):assign_combat_objective{
    objective = context:slot(mission.Slot.AUTHORED_OBJECTIVE),
    revision = 1,
    task_group = -1,
    reserved = false,
}
```

Use the actual generated declarations. Do not invent a squad or objective from a nearby name. The SDK resolver had to distinguish campaign and arcade occurrences; reusing a slot with the wrong occurrence can publish successfully while controlling the wrong native content.

`combat_ai.lua` consumes objective revision 1 and native task-cost arrays. Costs below 2040 are candidates; 2040 is the native saturated/unreachable bound. It selects the lowest cost and retains the current group on ties to avoid repeated relinking. Twelve five-bit group selections are packed into each durable integer. Fixed-task declarations, when present, are treated separately by the encounter.

`Encounter:enter` validates authored counts before staging placements. Its phase moves from 0 to 1 only once. Population tracking uses `seen` and `alive` masks so an initial zero report does not count as a kill. Phase 2 marks completion. A later visit does not call placement again. Reset is explicit and retires old members with native removal before new generation work.

Current Electron Controllers use `EMBER_APEX_SECURITY_OBJECTIVE` with seven task groups, together with the security/dispenser area. The earlier access-objective fixed-task experiment is superseded. `electron_controllers` is exactly `DISPENSER_SUPPORT_A_SQUAD` and `DISPENSER_SUPPORT_B_SQUAD`; support C is a separate encounter. Killing both controllers gates the Interceptor-area door. Their accessibility and tendency to move through that door were repeated user reports, so keep this on the final acceptance checklist.

## 8. Lever, Ghost console, bridge and ordinary devices

The landing door lever is an authored type-4 interactable object paired with a type-23 device. Rendering/interaction and animation are separate operations. Initialize the native interactable, unlock/power and establish the device baseline, then animate it on the exact accepted interaction receipt.

`landing.lua` checks the lever's registry key/type/index, sets `ember.lever.used`, transitions the lever device to `open`, enters `mercury_bonus`, and opens the paired door. Proximity must not spawn Crimson Shadow or open that door. Crimson Shadow is optional to the mandatory pre-bridge clear and must not hold the Ghost console disabled.

For ordinary devices:

```lua
slot:transition{transition = context.sdk.device_transitions.unlock}
slot:transition{transition = context.sdk.device_transitions.power_on}
slot:transition{transition = context.sdk.device_transitions.close, snap = true}
-- Later, on the proper event:
slot:transition{transition = context.sdk.device_transitions.open}
```

These are examples of API use, not universal endpoint polarity. The bridge specifically uses position 1/open for retracted and position 0/close for extended. The opening beam also has its own verified mapping. Always check the authored device instead of assuming “open” means physically open for every model.

The Ghost console is enabled only after the required opening squads have been observed and cleared. The bridge advances on the exact Ghost-link generation with inactive state and progress at least 1, not merely on an object interaction or combat clear. That receipt extends bridge devices, activates darkness, starts the far-side `sun` and `helipad` groups, and starts transport. Completed encounter state prevents a later bridge crossing from respawning them.

The close-side bridge squad is part of pre-bridge combat, not a surprise trigger after walking onto the bridge. Far-side hangar squads preload at bridge activation. Darkness completion considers the appropriate bridge/passenger/sun population; distant helipad population must not leave this area permanently restricted.

## 9. Harvester transport: exact current sequence

The ship and its passengers have separate native commands and receipts. Do not return to spawning passengers at world transforms inside a moving ship; that caused falling troops and premature deaths.

The four ships are A/B/C/D. A and B carry the four bridge passenger squads with manifest `{1,3}` and `{2,4}`. C and D are fly-through ships with no bridge passengers.

1. Before Ghost use becomes available, reserve passenger squads. Create empty ship parent squads with zero ordinary member counts. Assign the ships to the bridge combat objective. This prepares native ownership without an extra stationary spawn.
2. `play_actor_path{generation=..., revision=1, path=entry_sequence_slot}` creates/enters each authored ship member along its entry path.
3. Wait for that member's native revision-1 path-complete receipt. Current delay is `ARRIVAL_SETTLE_MS` 3000 plus `EXTRA_DROP_DELAY_MS` 3000. Preserve the actual six-second code value unless a new timed comparison justifies changing it; earlier conversation “3-second delay” referred to an added delay.
4. Call `deliver_squads{generation=..., revision=1, squads=reserved_sensor_slots}` for A/B. Delivery uses the native transport mechanism that opens the doors and detaches cargo. It is not a lever-device animation substitute.
5. Wait for delivery completion and actual passenger population receipts. Hold for a further 4000 ms at the authored drop position, then request revision-2 exit path.
6. On revision-2 path completion, invoke `play_actor_action` with revision 3, empty group hash `811C9DC5`, and action `7B0D3643` = FNV-1(`exit_25`). This is the Harvester's authored exit action from controller `80FE21CE`.
7. Retire the actor with a newer generation after the exit-action completion receipt, or when its dead receipt arrives. Do not delete it before the effect or leave it parked at the path end.

`ember.transport.flags` packs five flags per ship. Timers include generation so a wipe cannot let old departures affect new ships. Existing flights are allowed to finish and retire after the player leaves the region. The native actor transport/action hooks and schemas under `Sunrise/src/` are part of this implementation; copying Lua onto an older upstream DLL will not reproduce it.

## 10. Refinery cell and later route

`processing.lua` initializes the drill open with the obstruction present. Accepted fusion-cell deposit drives the next phase, closes the drill/machinery as scripted, sets the Processing checkpoint and starts defense. The end-of-drill hatch opens when reaching its area, because its enemies are beyond that hatch; it must not wait for those inaccessible squads to die.

`carry.lua` is shared by refinery and final reactor cell:

- Enable the authored pickup and receptacle with `set_interactable_object`.
- Track pickup owner and exact generation through native Sense.
- Accept deposit only from the receptacle's current generation after a pickup receipt.
- Set done before consumption/recovery side effects.
- Consume the carried cell with a newer inactive object generation. Interaction alone did not remove it in earlier builds.
- If an uncompleted cell disappears or becomes unusable, arm a two-second recovery timer. Advance pickup generation by two, leaving the receptacle generation unchanged.
- Do not recover while its region is unloaded; defer and resume after return.
- Ignore stale receipts. Once deposited, recovery remains disabled.

Ownership fields require their actual native wire representation. An earlier optional/raw64 decoding mistake prevented valid owner state, and an inverted use flag removed the prompt. The successful current API should be preserved when debugging later cell behavior.

`cinder.lua` advances through explicit authored triggers and durable phases. The indoor Sunside darkness/dialogue boundary is beyond the far door, not while still outside. Checkpoints include ready room 2 and Foundry. The foundry callback uses exact group comparisons compatible with the sandbox.

## 11. Reactor mechanics, effects and remaining presentation questions

`apex.lua` owns access/security, reactor sides, core, mother-brain cell and escape. Both Electron Controllers gate the Interceptor-area door. Reactor bridges start retracted and extend after both side targets are destroyed. The central doors join the cooling cadence after their unlock condition. The central target must be actually placed, damage-watched and killed before the carry route unlocks.

Destruction requires native evidence. Missing health, an initial zero or a stale generation cannot count as destroying a target. The object/damage decoder fixes in the DLL are essential to the Lua progression.

### Beam and cooling

The current cycle is:

- closed/resting: 14,000 ms;
- warning/surge: 6,000 ms;
- open/cooling exposure: 10,000 ms.

Surge ends **before** the clamshell doors open. This is a cooling mechanism. The user's failed inversion request was undone: current `beam_pose` uses `open` for normal and `close` for surge, applied to both laser and ring devices. `beam_surge` owns the rising edge and queues the authored alarm at that same transition; independent audio pre-roll experiments are superseded.

Beam objects and their device power must both be enabled. Presence handling seeks the driven endpoint once and animates back to the normal endpoint so authored events can run. It is generation/idempotence guarded and must not restart the beam after final deposit. Deposit shuts the beam down.

The user repeatedly reported that sound lagged the visual by about four seconds. Current code ties their requests together, but internal animation/audio offsets can still differ. Do not equate simultaneous Lua calls with audible synchronization. The exact full-screen surge effect is another outstanding verification item; no unrelated Omega event should be substituted just because its identifier is available.

### Scorch

The latest source uses one native player attachment, borrowing the authored Sunside sunburn asset `80B82489` through `bootflow/ember_sunburn.cpp`. It narrowly recognizes Ember type-26 slot 43's captured component/template identity, replaces the per-component spawn-request asset for the native attach call, then restores it. It does not patch a shared package or apply an arbitrary damage multiplier.

The placed `SUNBURN_DAMAGE_OBJECT` remains disabled to avoid a second source. Before deposit, the same attachment is filtered to the five authored narrow hot-pipe volumes. During escape it uses the rail-top player filter. The mode is idempotent and the effect is disabled at the escape trigger/reset boundary. Earlier experiments with a rail effect plus pipe effect, a delayed reattach, or only the damage object are superseded.

The user reported both doubled damage and absent escape scorch across prior candidates. Verify actual damage rate and scope in the current build. A successful `ember_sunburn result=attached` receipt proves an attachment, not its contact geometry or correct damage amount.

### Explosions and extraction ship

Escape explosions use a single live Scene generation. As the authored A/B/C/D trigger volumes are crossed, `set_scene_events` republishes the accumulated matching event keys (`explosion_set_a_trigger` through `_d_trigger`). Scene activation without those inputs did not fire the authored effects. Duplicate trigger receipts must not restart an explosion.

The extraction ship uses its own placed object/device path, distinct from the combat Harvesters. Its actual model is `80B71228`, with device component `80C70C0E` and animation component `80B71226`; the shared fallback `80BFDDC2` is not the placed ship. Presence is followed by unlock/power/baseline/animated drive once. Its visual authored flight still belongs in the final checklist.

## 12. Objectives, navigation, dialogue and music

Each generated directive element contains the main title and sub-objective. Main wording can legitimately remain “Find and disable…” through much of the mission, then change at reactor and escape milestones. Repeating the same element is not a main-objective update.

Type-68 state 0 enters the directive; repeated publication replays the banner animation. `route_support.lua` deduplicates on the directive hash plus its stable marker. It tracks furthest region rank so backtracking does not regress the banner. Navpoints are authored type-47 references; combat-aware guidance is handled by the relevant controller.

`ember.hud.audience.<region>` is only a once-only flag for initializing the type-70 engagement sensor used as the directive audience. It is **not** a HUD visibility switch, cinematic state or video mask. Native engagement flag bit 0 clear includes active players.

Dialogue cues are authored `M_DIALOG_SENSOR_80B3C90A` cue declarations, with durable deduplication keys. Music uses the native type-11 sensor and sections from bank `80B3C904`, with 29 ordered sections. Section-to-encounter pacing was reconstructed from package order and mission milestones. The music bank owns the transitions, but a complete audible retail-equivalence check has not been established.

## 13. Darkness, respawn and checkpoint reset

`wipe.lua` treats “all dead” as positive dead count, zero alive count **and zero unknown count**. Unknown membership must not wipe an apparently incomplete fireteam.

Stage 1 publishes a three-second type-35 darkness countdown and ticks it once per second. If someone becomes alive or darkness clears, cancel it. At zero, `restart_checkpoint` begins the native hard-wipe handshake. Once native spawn state reaches teardown/fade state 2, reset the relevant encounter and issue the release request. Native state 0 after release completes the reset.

Authored checkpoint spawn sets used by the route:

| Checkpoint | Region | Spawn-set hash |
| --- | ---: | --- |
| Landing / bridge | 64 | `9C58857A` |
| Processing | 56 | `4B27745D` |
| Ready room 2 | 40 | `82328D63` |
| Foundry | 40 | `782CAF4C` |
| Reactor entrance | 0 | `DF59C25C` |
| Escape | 0 | `45920385` |

Resetting encounter state on a routine region enter would violate the one-shot requirement. Keep wipe handshake, encounter reset and ordinary traversal separate. Do not simulate the requested 30-second respawn by merely withholding a spawn location; the user's requirement is a visible native timer.

## 14. Why the ending is a direct pre-rendered movie path

The opening is an in-engine type-6 cinematic. Its controller owns an authored region and emits exact started/terminated/skip incidents. The two ending bookends use pre-rendered movie components:

| Movie | Placement | Entity | Component | Wrapper | Stream |
| --- | --- | --- | --- | --- | --- |
| STM, first | `80B3C222` | `80B38179` | `80BDDC62` | `80BCA001` | `80BCA034` |
| CNN, second | `80B3C226` | `80B3817B` | `80BDDC67` | `80BCA003` | `80C7C000` |

Both entities contain class `808065EB`, configured by `808065EC`; config+`4C` carries the movie asset. The package/native evidence is more reliable than guessing from cinematic slot names.

Earlier attempts selected bookend regions 1/2, tried region 49 as staging, or waited for seeded type-6 authority. Those encountered native world teardown/record-chain failures before a movie controller existed. The Omega guide describes an in-engine cinematic whose received authority can wait behind a global readiness gate. That finding is useful, but is not proof that a pre-rendered Ember movie needs the same actor/controller workaround.

Current escape handling keeps Apex loaded and calls the native movie manager on the game frame. It does not fabricate a type-6 actor, bypass general readiness, drain global I/O, or complete the mission before playing the videos.

Public mission API:

```lua
context:play_prerendered_movie{index = 1} -- or 2; optional stop request
local status = context:prerendered_movie_status(1)
```

Requests carry session, ActivityClient generation and request identity and are restricted to the appropriate private Ember/Apex context. The native bridge lives in `Sunrise/src/client/hooks/ember_movies/`; frame observation is installed by `bootflow/ember_movie_tick.cpp` and also uses player-camera frame observation.

`ending.lua` polls every 250 ms. Queued/preparing do not count as playback. First native completion queues the second movie. Second native completion sets `ember.complete`, phase 100 and lifetime 6. Other gameplay route dispatch stops once `ember.ending` owns the route.

## 15. Native movie playback and resource ordering

All addresses below are RVAs in the captured mapped build, with image base `140000000`. Production calls resolve unique signatures and verified relative calls; do not blindly hardcode these addresses into another version.

| Operation | RVA |
| --- | --- |
| Native pre-rendered component start | `DDB0F0` |
| Movie manager accessor / acquire | `41B040` / `41A3C0` |
| Play(manager, asset, 0) | `41CD20` |
| Stop / release | `41D0C0` / `41A980` |
| Native busy predicate / decoder accessor | `41B420` / `41AB70` |
| CPU movie frame processing | `41D140` |
| Surface selection publication | `1202B00` |
| UI movie command submission | `132B890` → `1278FF0`, command `1B` |
| Resource manager/create/add/submit | `4294D0` / `423EF0` / `4312D0` / `435AA0` |
| Root status / destroy | `42C650` / `425310` |

The decoder's asset is at `+1B4` and state at `+1B0`. The bridge requires state 5 for the exact asset to recognize playing. Completion requires that playback was previously seen, native busy is false, and decoder state is stopped/end (0 or 6). State 7 is failure. A changed decoder/asset/world fails instead of awarding completion. Prepare/busy/resource waits are bounded at 30 seconds; playback at 600 seconds.

### All movie resources use kind 1

Kind 2 means shared/type-16 resources. Copying it from a startup loader caused a native crash. The metadata, compact stream mapping, surface definitions, buffers and containers here all use package-verified kind 1.

The stream request does **not** read an entire video into RAM. Native `3591B0` recognizes the stream type and publishes encoded file offset/patch and length. Omitting it left valid wrappers with an unopened stream and decoder failure state 7.

### The six authored Y/U/V surfaces

| Slot | Plane | Definition | Buffer | Container |
| ---: | --- | --- | --- | --- |
| 1 | Y0 | `80BCA021` | `80BCA020` | `80BCA022` |
| 2 | Y1 | `80BCA024` | `80BCA023` | `80BCA025` |
| 3 | U0 | `80BCA026` | `80BCA027` | `80BCA028` |
| 4 | U1 | `80BCA029` | `80BCA02A` | `80BCA02B` |
| 5 | V0 | `80BCA02C` | `80BCA02D` | `80BCA02E` |
| 6 | V1 | `80BCA02F` | `80BCA030` | `80BCA031` |

Catalog `80BCA032` names those containers. A container is class `80806B91` and contains the definition reference. Definitions are package type `44FB`, buffers `254FB`, both subtype 19. The definition file has eight bytes, but native allocation expands it to 16; the backing pointer lives at +8. Native buffer callback `1204581` fills that pointer. The renderer reads it via `4A6340`.

Load order is essential. A container registration callback at `1204163` immediately dereferences its definition's slot byte. A single asynchronous batch with “definitions listed first” does not order the callbacks. The `a47adbd` run faulted there with RAX 0 and RBX `80BCA021`.

Current `MovieResource` therefore uses two roots:

1. Load only six definitions and validate their native resident size/type, slot and format.
2. Load both movies' metadata/compact stream mappings, shared catalog, six backing buffers and six containers.
3. Wait for ready roots, valid wrappers/media, non-null backing pointers and registered surfaces.
4. Publish the native surface-selection stacks after the movie player is idle, then verify all six selected definitions before acquire/play.

The registration table is at captured global `142E800B0`, eight rows of 20 bytes: count, three candidate handles, selected handle. Nonzero stale candidates with count zero are not registrations. A CPU frame and sound do not prove those GPU surfaces exist.

## 16. Shared resources, EOF and orbit

Both movies share the same textures. `15e507e` withheld first completion until those textures disappeared after root release; the loader retained them, so Lua never queued movie two. The captured first movie reached EOF and then stayed at `surfaces_retiring`.

Current code exposes native EOF immediately, balances that movie's manager acquire, and keeps the shared resource set for CNN. A held set can only chain movie 1 → movie 2 for the same session/generation with a recorded first completion. Another owner, reversed/repeated sequence or preparing/failed state cannot borrow it. Native cleanup runs independently after the final movie. It must never gate the mission's completion on global cache eviction.

`orbit_return.cpp` arms only after both movies completed for the same owner. It waits for native lifetime 6, confirms the same world arrival and selected Ember destination, constructs the native default orbit selection, clears/selects/commits it, and waits for the destination to read back as orbit. Then it requests deferred native cleanup, step 28, and recognizes orbit setup at step 29. There is no completion-banner hold.

The last tested timeline:

| Time in log | Receipt |
| ---: | --- |
| 281530 | STM playing, state 5 |
| 443845 | Foreground Escape requested native stop |
| 443950 | STM complete; CNN queued |
| 444277 | CNN playing, state 5 |
| 649483 | CNN natural completion; orbit handoff armed |
| 649544 | Lifetime completion accepted; orbit selection queued |
| 652554 | Deferred native cleanup requested |
| 653295 | `orbit_setup` |

This validates sequencing and native orbit setup. It does not retrospectively validate the obscured video, HUD/menu behavior, or final resource eviction. In particular, the captured log did not establish a final `resource_released` receipt before shutdown; retain that distinction if investigating repeat-run resource ownership later.

## 17. The loading overlay mistake and the exact correction

The removed early workaround detoured UI layer drawing at `132BD80` and returned without drawing anything during movies. It hid HUD, but also hid subtitles and normal UI. The user asked to use native cinematic state instead.

`41e2728` then mapped normal gameplay requests (`0x2B`, `0x2D`, `0x2F`) to `0x22` through the original native UI state transition at `E1CD60`. Both `0x21` and `0x22` belong to category 4. Checking only that category was insufficient.

The complete mapping now verified is:

```text
UI state 0x21 -> 1312540 -> window enum 26 -> 13126E0 -> 17A73819 = cinematic_overlay
UI state 0x22 -> 1312540 -> window enum 29 -> 13126E0 -> D505DEBB = loading
```

Names use FNV-1, not FNV-1a. Native window manager `1316FC0` reads the UI state, calls those mappings and explicitly excludes `0x22` from its cinematic-overlay branch at `1317094`.

The live UI capture showed `loading` asset `80B46D88` alongside `subtitle_overlay` asset `80BC6681`, hash `7737E414`. Six valid video surfaces were present. Thus this black screen was a real UI window covering video, not a return of the texture problem.

**`88a9632` changes the substitution to `0x21`.** It leaves other requested UI states alone, retains presentation across the movie handoff, and clears it on final completion or failure. Native drawing, window updates and input remain intact. The tests now follow state → enum → actual name hash and check that true loading/menu requests are preserved.

The user briefly opened menus for a read-only capture on the old build, but that only caught a short-lived additional window while the loading state was active. It is not a validated inventory/settings allowlist.

The next `88a9632` test left the gameplay HUD over the video. Native `1316FC0` independently creates `hud` (name hash `268AB804`) or an equipment-specific override, then assigns semantic role 18 through `13165C0`. That setter writes window offset `310`; it is distinct from the state enum at `410`. The current `bootflow/ember_movie_hud.cpp` filter checks this role only at the verified root-window draw caller `132C1BD` → `13D9060`, returning before the entire HUD subtree, including cached child commands, is submitted. Recursive child widgets use the same callee but do not have a full window allocation, so the exact return-address guard must precede the role read. Do not remove that guard.

Both `132BD80` layers and every other window role continue drawing. Native updates/input/visibility and user preferences remain unchanged. The filter is active only while the ending bridge owns presentation, spanning the movie handoff and ending on final completion/failure. It reports `hud_filter_attached` at startup and `hud_draw_suppressed` with the actual window identity on its first suppression. The native verifier checks the root construction, caller/callee and role setter; the user confirmed the HUD fix works in game. This is narrower than the old blanket layer suppression. Latest log and disassembly evidence: `build/first-encounter-audit/b9880dc-hud/`.

A remaining interaction concern to test: the direct player uses a foreground Escape edge to call native stop. Native in-engine cutscenes have a type-6 skip incident; direct movies do not. Verify that closing inventory/settings does not accidentally skip the movie. If a change is needed, base it on native menu/input ownership rather than simply consuming all Escape presses.

## 18. Launch suppression and Lime

`bootflow/loading_cinematics.cpp` detours `LoadingCinematics_Suppressed`, captured RVA `C24490`, using a verified caller signature. It honors `client.suppress_loading_cinematics` only when the current native selected activity resolves to definition `38F926B2`. It reads the selection each time; a retained host session or saved character activity is not proof that Ember is being launched now.

This is travel fly-in suppression, not movie playback authority or the cinematic presentation state. The mission intro still starts through its own controller.

A previous Lime investigation also involved incorrect native alternate-world assumptions. Base and alternate worlds have separate registrations (`4C8E60` / `4C8E40`); mutating the message-1 per-bubble state byte to select a bookend was wrong. Do not restore that wire change or globally suppress loading cinematics.

## 19. Bird, simulation records and cleanup

Bird was not established as a single error with one universal cause. Captures included exhausted simulation records, detached authority records, blocked transmission, wrong purge epochs and later teardown stalls. Do not “fix Bird” by blindly increasing a pool or enabling darkness everywhere.

Important retained fixes include:

- Zero-count squad replacement uses native destruction with a newer generation, not merely a kill-only operation.
- Membership publishes the remote-host transmission bit used by native entity views. Acknowledgements alone could advance while entity transmission remained disabled.
- Abandoned-slot/purge requests receive exact-mask native purge responses with the entity authority manager's own epoch. This is distinct from another replication epoch field.
- Valid transport acknowledgements release outgoing contributions independently of unsupported incoming gameplay lanes.
- Retired roster keys keep wire ordinals while their presence bits clear; bodies for retired keys are omitted.
- Encounter removal/reset stays bounded and generation-qualified.

Useful logs: `ev=simulation_records stage=pressure`, `ev=entity_replication stage=census`, mission intent refusals, incorrect epoch assertions and native watchdog exceptions. The simulation-record capacity observed in the current run is 1024. The portable tests cover the wire and ownership rules; live counters and actual reclamation must still be inspected when changing them.

## 20. Build, tests and installation

### Windows DLL from this Linux workspace

The working setup uses CMake/Ninja, clang-cl, lld-link, llvm-rc and an xwin SDK. `linux-to-win-toolchain.cmake` refers to `.xwin-cache`; that directory is local toolchain state and is not included in the ZIP. Supply a compatible xwin tree with CRT and Windows SDK layout before configuring a fresh build.

```sh
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=linux-to-win-toolchain.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build --target steam_api64 -j 6
```

The existing cache uses Windows SDK `10.0.26100`. The target is C++20, including the embedded Lua sources built with the project's C++ exception configuration. The tested build path is CMake; do not assume Visual Studio project item lists are automatically synchronized with the globbed CMake source list. Do not start a second linker while the first is still running.

### Portable C++ regressions

```sh
cmake -S tests -B build/portable-tests
cmake --build build/portable-tests -j 4
ctest --test-dir build/portable-tests --output-on-failure
```

There are currently 24 tests. Relevant coverage includes actor paths, objective/AI support, object interaction, directives/navpoints, darkness/life, effect/damage/music schemas, cleanup/ACK handling, scene events, and `ending_retirement_test` for movie resources, sequencing, native UI selection and orbit guards.

### Lua route regressions

Use the SDK provided in the branch archive or the installed SDK. The route test accepts an explicit module path:

```sh
lua tests/mission_ember_routes_test.lua /path/to/Sunrise/sdk/lua/missions/mission_ember_80b3c09e.lua
```

Other suites are `mission_ember_encounter_test.lua`, `mission_ember_controller_test.lua`, `mission_ember_combat_ai_test.lua`, and `mission_ember_wipe_test.lua`. Check each harness's SDK argument/default when running outside the original workspace. No game launch is involved.

### Native ABI/package verification

```sh
python3 tests/verify_ember_movie_native.py build/first-encounter-audit/game_image.bin /path/to/game/packages
```

The image must be the mapped/decrypted image, not the encrypted on-disk executable. The verifier checks unique signatures, relative call targets, resource kinds, stream mappings, surface definitions, backing callbacks, movie and orbit operations, sunburn attachment, and the actual cinematic/loading window mapping. Small extracted tag files in `build/first-encounter-audit/tags/` support the package checks. Image-specific checks must be re-established after a game executable update.

### Install

A durable installer is now committed at `tools/install_mission_ember.py`; it replaces the old dependency on `/tmp/install-ember-apex-cooling.py`.

```sh
python3 tools/install_mission_ember.py --game /home/millie/Games/Sunrise/bin/x64 --dry-run
python3 tools/install_mission_ember.py --game /home/millie/Games/Sunrise/bin/x64
```

For the DLL carried in the archive, add `--dll _snapshot/runtime/steam_api64.dll`. It expects Linux `/proc` to enforce the closed-game guard, backs up the 18 destinations, verifies hashes, uses atomic replacement per file and never launches/stops the game. It does not install SDK/settings/save; the snapshot's matching SDK is separately supplied for a new environment. In this managed workspace, writing to the game directory requires tool escalation, but installation authorization already exists.

## 21. Regression checks and debugging decision tree

1. Have the user manually launch the installed HUD-filter DLL; its SHA-256 is in section 1. Confirm `frame_attached`, `presentation_attached` and `hud_filter_attached` in the new log.
2. Reach the final escape trigger normally. Confirm one movie-1 request. Preserve the current trigger and progression if it already fires.
3. Expect requested gameplay state 43 to select/applied state **33**, not 34, and require `hud_draw_suppressed role=18` during playback. Check that the picture is visible, reticle/ammunition/abilities/radar/objective HUD is absent and subtitles appear when enabled.
4. Open inventory and settings; verify normal input and rendering, including closing them without unintended skip. The native state is preferred to a new mask, but this behavior has not yet been observed on the new build.
5. Test first-movie natural EOF in one run and explicit skip in another. The last run established first skip and second natural EOF, not every combination.
6. Verify second-movie picture and subtitles; require native `playing movie=2`, not just its queued/submitted log.
7. After second EOF/skip, require completion and orbit setup. The return request should not wait through a completion-banner hold.
8. Check final cleanup receipts and a second mission launch in the same process if investigating retained resources. Never force-free native definitions while containers can still call them.

If black returns, classify it before changing code:

| Observation | Investigate |
| --- | --- |
| Decoder never reaches 5 | Resource requests, correct stream mapping, player ownership and decoder error |
| Audio/CPU frames but zero selected/backed surfaces | Definition/buffer/container residency and publication |
| Valid surfaces plus named `loading` window | UI state/window selection; do not rewrite the decoder |
| First EOF but no second queued | Completion exposure and same-owner resource handoff |
| Both movies complete but no lifetime 6 | Lua ending poll/intent delivery and owner generation |
| Lifetime 6 accepted but no orbit | Native selection readback, deferred cleanup and orbit state receipts |
| Frozen game before any movie request | Escape dispatch/world teardown; capture thread/watchdog state read-only |

Keep read-only evidence scoped to the relevant process/module and never recover by blindly writing memory. Existing historical capture helpers under `/tmp` are not part of the interface and may contain a stale PID. Inspect them before reuse. The archive includes the important captured evidence so analysis need not depend on those processes or helper scripts still existing.

## 22. Handover completion checklist

- The delivered archives remain source `b9880dc` with installed gameplay `88a9632`. This working tree additionally carries the HUD correction described in sections 1 and 17; do not assume it is in the old ZIPs.
- This document, the reusable installer and the two pre-existing untracked handovers are included in the final documentation commit, with no co-author trailer.
- Branch ZIP includes all committed files and branch history; scripts ZIP includes all 17 Lua files.
- Runtime DLL, matching generated SDK and selected diagnostic data are supplemental branch-archive assets with checksums.
- Archive verification and the exact final commit are recorded in the delivery manifest.
- No game launch, stop, new gameplay change or settings/save replacement is part of packaging.
- The user confirmed the role-18 HUD correction works perfectly on `3fea388`. Preserve it; use section 21 for regression checks, not as a claim that every menu/skip combination was exercised.
