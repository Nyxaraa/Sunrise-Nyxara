# 1AU final issues: handoff and implementation plan

Updated 2026-09-06. User: Millie. Mission: Destiny 2 **1AU**, internal **mission_ember**. Scripting: Lua, using Sunrise's generated SDK and native authority support.

## Start here

The user has completed nearly the whole mission. Preserve the working opening, dropship doors/exits, bridge, enemy spawning, carry interactions, reactor target progression and darkness-zone mechanics. The latest implementation did **not** fix the remaining visual/native problems. Do not interpret passing unit tests or earlier status notes as gameplay confirmation.

Repository: `/home/millie/Documents/Sunrise-builds/mission-ember`, branch `mission-ember`.
Latest implementation commit: `50e68b7` — `Implement 1AU mission scripting and native encounter support`. It contains all prior mission/runtime work (130 changed files), without a co-author tag. Working tree was clean before creating this document. No implementation changes were made after that commit during the subsequent planning conversation.

Installed game: `/home/millie/Games/Sunrise/bin/x64`.
Installed scripts: `Sunrise/scripts/mission_ember.lua` and `Sunrise/scripts/mission_ember/` below that directory.
Log: `/home/millie/Games/Sunrise/bin/x64/Sunrise/logs/sunrise.log`.
Latest install manifest: `build/finale-followup-installation.json`.
Backup: `build/finale-followup-backup-20260906-112405`.
Installed DLL SHA-256: `1ca1f37ea2735aeb0dab6d4a7522937e3cc9c66a5a09b297448253081887f97c`.

### User constraints

- **Never launch or stop the game.** Millie launches manually. Read-only captures are permitted. Check the game is closed before installing.
- Use Proton for standalone package extraction if necessary; do not launch through Wine. The extractor is not the game.
- Preserve save, settings and SDK. Back up and verify installation. Installation outside the workspace requires tool escalation, but user authorization to install persists.
- Give concrete progress updates at least every minute. The user has repeatedly objected to silent investigation/stalls.
- Completed areas must remain completed on backtracking. Only a confirmed checkpoint wipe resets its encounter.
- Do not revisit tube-entry deaths: the user identified those as a noclip issue and explicitly withdrew that request.
- Commit without a co-author tag when committing further work. Do not push unless requested.
- The current request is to prepare this handoff plan, not to implement another speculative patch.

## Latest user findings — authoritative acceptance criteria

1. Electron Controllers still warp out of the Interceptor room and become inaccessible behind the door. Both must remain reachable, and killing both must open the gate.
2. Escape currently applies the wrong hazard: **shock**. It must apply **fire/scorch**.
3. **The pipes on the climb toward the fusion-cell deposit must apply that same fire/scorch effect**, before the deposit/escape phase. Restrict it to actual hazard contact and clear it on leaving.
4. The beam must disappear after the final fusion cell is deposited.
5. During the reactor fight, the beam currently stays visually consistent. It must surge and trigger the corresponding screen effect before clamshell exposure.
6. Explosions must trigger progressively as the player performs the escape, not all at deposit.
7. Escape still freezes before the ending cutscenes render. Both authored ending movies must play and complete, including native skipping.
8. Mission objectives now repeatedly flash/re-update without actually changing. Main title and sub-objective must progress once at their intended milestones, independently of navpoint changes.

## Reference and evidence

Gameplay reference: https://www.youtube.com/watch?v=PqurUhqC2CE
Intro cinematic: https://www.youtube.com/watch?v=IUmUYHELJAY
Reference repository: https://github.com/stanuwu/SunriseMissions

Actual gameplay video is cached at `/tmp/ember-reference/1au.mp4` (640x360, ~36:42). Prefer reviewing the cached footage to relying on recollection. Relevant reactor/finale segment: **21:20–28:30**, plus escape completion around **28:42** and the following movies.

Existing contact sheets under `build/reactor-audit/`: `reference-01..08.jpg` (5-second samples), `vent-01..05.jpg` (23:28 onward, 1-second samples), `core-01..06.jpg` (25:32 onward), `carry-01..05.jpg` (27:15 onward).

Observed reference timing:

- 23:38: white/blue screen pulse.
- 23:40–42: blue beam intensification; 23:43 bright orange; 23:44 shutters begin opening; ~23:45 target exposed.
- Another pulse around 24:09. Current 14s closed / 6s warning / 10s open timings were reconstructed from video, not recovered original mission code.
- 26:30: central target destroyed.
- 27:37: final cell held; 27:45: glowing hot pipes burn the player during the climb.
- 27:58: deposit; 28:01: overload; 28:05 onward: escape; ~28:42: ending transition.

Local documentation: `/home/millie/Documents/Sunrise-docs/MissionDocs/`.
Relevant files: `OMEGA-FOREST-ENEMIES.md`, `OMEGA-LAIR-CINEMATIC.md`, `OMEGA-NAVIGATION.md`, `1AU-SCRIPTING-AND-AI.md`. The local Omega cinematic doc may not contain the newest recovery described below; the user supplied it externally.

### New Omega evidence from the user

> The received cinematic authority at record `33F26015` had revision 11 / play 1 pending, while the native controller remained at revision 0. The captured native readiness gate correctly blocked application because 19 allocated global records had never been seeded.

The user reported that a bounded live recovery allowed playback, followed by native completion at `t=1408781`, about 171.8 seconds after starting: `phase=5 revision=14 play=0 complete=1 failed=0`; then encounter `ending_finished phase=10 crown_stage=14`.

**Interpretation:** authority receipt and native application are different stages. This proves playback after that recovery in Omega, not an automatic installed-build fix and not the cause of 1AU's freeze. Do not copy Omega's record ID, revision or count into 1AU. Derive the actual missing records and the publication path that should seed them.

Previous 1AU capture: `build/reactor-playtest/20260906-104913.log` and `20260906-104913-events.log`. It reached the final deposit and escape endpoint. A teleport was armed, but no first-ending cinematic-start event followed. Native diagnostics repeated `network_send` stalls. The newer game log also ends in stall snapshots after testing the staging workaround. Preserve that log before it is overwritten.

Repeated scanned stack addresses included `exe+17354B7`, `exe+173A58B` and native worker waits. These are scanned stack words, not a reliable unwound call chain. Decompilation of 1735460/173A560 showed communication/wait machinery; it did not establish the freeze's root cause.

## Implementation order

### 1. Diagnose and fix ending readiness/publication

Files:

- `scripts/mission_ember/ending.lua`, `routes.lua`, `apex.lua`
- `Sunrise/src/server/activity/mission/mission_script_runtime_dispatch.cpp`
- `Sunrise/src/server/activity/activity_sdk_mission_runtime.cpp`
- `Sunrise/src/server/bap/bap_route.cpp`
- `Sunrise/src/server/bap/encrypted/push/activity/activity_mission_seed_roster.cpp`
- Cinematic codec, roster publication, authority cleanup and diagnostics modules in the current commit.

Current ending flow selects an unplayed intro cinematic state (region 49), waits for held-region acknowledgement, then selects movie region 1; repeats staging before region 2. This detour was based on a same-bubble teardown hypothesis. **It still freezes. Do not keep adding staging states or arbitrary sleeps.**

Target states/slots:

- First: `STATE_80B3C09E_0000_0001_80B3C091`, region 1, `PF_CINEMATIC_BOOKEND_STM_CINEMATIC`.
- Second: `STATE_80B3C09E_0000_0002_80B3C093`, region 2, `PF_CINEMATIC_BOOKEND_CNN_CINEMATIC`.
- Current staging: `STATE_80B3C09E_0006_0001_80B3C09A`, region 49.

Plan:

1. Capture requested/current/held region, seed-lease plan/revision, publication state, cinematic received authority and applied controller state at the failure.
2. Inspect the native cinematic readiness predicate. Enumerate allocated global records, initialization/seed state and owning schema/registry. Establish whether authority reaches the movie at all before pursuing the Omega mechanism.
3. Map any missing seeds back to canonical/global/selected-state roster construction. Fix the actual producer or ordering. Never fabricate arbitrary zero bodies or bypass readiness globally.
4. Ensure every required record is seeded with its schema-valid body and old-area records can retire without deadlocking publication.
5. Replace the failed staging workaround with the correct authored transition once the evidence supports it. Keep exact completion/skip identity checks and one-shot progression.

Acceptance: automatic first movie start, native finish/skip, second movie start, native finish/skip, final lifetime completion; no freeze, Bird, permanently pending readiness or resource growth. A successful manual memory recovery alone is insufficient.

### 2. Stabilize HUD objectives and advance their actual text

Files: `route_support.lua`, `landing.lua`, `mission_script_lua_slot_api.cpp`, `activity_scriptable_auth_fixed_body_codec.cpp`, `scriptable_auth_body.h`.

Current API sends type-68 directives, always using lane 0, with generated name hash/element and optional type-47 navpoint. Latest patch added an `audience` reference to type-70 engagement sensors and `set_engagement_state{flags=0, revision=1}` per region. Region bindings: landing `M_ENGAGEMENT_SENSOR_80B3CB5E`; Processing `...80B3C8F7`; Cinder `...80B3C22D`; Apex `...80B3C21C`.

Native evidence: `1009740 -> A76E90 -> 9F1290` checks engagement membership; `9F2A00` updates active-player membership. **This receiver is type 70, not a type-34 object filter.** The latest binding caused flashing without correct progression, so the prior inference is incomplete.

Plan:

1. Log actual outgoing directive hash/element/state/lane and audience, alongside native applied HUD entry and engagement membership changes.
2. Distinguish repeated script publication from repeated native banner activation with unchanged authority.
3. Confirm generated title/description mapping and native lifecycle/active-lane semantics. Trace `1009C00`, `10098C0`, `1009ED0`, `1008B40`, `1008570` as needed.
4. Separate objective enter/complete transitions from navpoint visibility changes. Current signature is `hash .. ':' .. shown`, so combat/navpoint changes republish the entire directive.
5. Fix the actual banner lifecycle and stable audience; do not hide flashing with an arbitrary debounce while leaving text incorrect.

Acceptance: correct main title and sub-objective at Processing, Cinder, reactor targets, final cell and escape; idle/combat/navpoint changes do not flash/re-enter the objective; backtracking does not restore an earlier objective.

### 3. Resolve Controller combat/teleport areas

Files: `route_roster.lua`, `route_support.lua`, `encounter.lua`, `combat_ai.lua`, `apex.lua`.

`electron_controllers` = `DISPENSER_SUPPORT_A_SQUAD` and `DISPENSER_SUPPORT_B_SQUAD`. Assigned `EMBER_APEX_ACCESS_OBJECTIVE`, 14 task groups. Latest patch pins the two squads to `fixed_tasks={9,10}` and skips later cost-based changes. **That mapping was inferred from ordering and failed in game. Remove the assumption; do not freeze all AI.**

Cached descriptors:

- Access objective config `80B3C361`, descriptor offset 2216, 14 groups.
- Security objective config `80B3D606`, offset 1752, 7 groups.
- Registry `A36972D4`, root `80B3C220`.
- Access groups 9/10 refer to type-45 task slots 40/41, named `__influencer_firing_area_set_12/13` in `80B3C21D`. Names/order do not prove correct geometry.
- Controller actor class `80C1A8E4`, behavior config `8162C450`. A squad anchor is approximately (-363, 2643, 181.5).

Capture actor position, selected objective/task, warp/custom behavior and candidate areas before/after escape from the room. Resolve authored firing/teleport areas and constrain the responsible behavior to reachable room positions. Determine whether the warp is objective-driven or a separate actor behavior. Both deaths, not vehicle boarding or unrelated supports, must open `SECURITY_DOOR_DEVICE`.

### 4. Recover the actual beam surge and shutdown controls

`apex.lua:beam()` currently drives position and power for `SPECOPS_APEX_RING_LASER_DEVICE` / `SPECOPS_APEX_RING_RING_DEVICE`; neither trial produced the intended surge. Do not assume another polarity or more power toggles will trigger the missing screen event.

Cached root `80B3C21C`, registry `A3B76C64`:

| Object/device | Slot | Config | Model |
| --- | ---: | --- | --- |
| Laser object | 45/type4 | 80B3D4E2 | 80B7117C |
| Core object | 46/type4 | 80B3D4E5 | 80B711C6 |
| Ring object | 47/type4 | 80B3D4E8 | 80B71218 |
| Laser device | 48/type23 | 80B3D4EB | — |
| Ring device | 49/type23 | 80B3D4EE | — |

Useful cached components: laser `80B71179`, `80B7117A`, `80C70C0E`, `80C77D05`; core `80B711A6`, `80BFCF84`; ring `80B711EB`, `80B71216`, `80C77DA4`. These are leads, not confirmed effect triggers.

Trace the placed-object effect graph inputs, relevant native events/sequences, and the separate player screen effect. Match video surge -> screen pulse -> shutter exposure. Verify resource references actually resolve and authority is applied to the intended placed instance.

On successful final deposit, explicitly stop the cycle and shut down the actual beam object/effect. Current deposit code erroneously sends both devices to true/open. Preserve the required escape geometry; do not delete an entire shared model indiscriminately. Restoring the escape checkpoint must keep the beam off.

### 5. Apply fire/scorch and progressive escape explosions

Current `hazards()` attaches `REACTOR_MOTHER_BRAIN_HOT_PIPES_THERMAL_HOP_ON` and `AOD_REACTOR_RAIL_TOP_HOP_ON`; the user sees shock. Identify which resource causes it. Verify the correct fire/scorch resource and native damage/status behavior, not just a suggestive slot name.

**Timing:** climb-pipe scorch must already work while carrying the cell toward the deposit (phase 5). Current setup runs only after deposit (phase 6), which is too late. Escape fire is enabled at its authored timing. Leaving contact, checkpoint resets and ending transit must remove effects correctly.

Global root `80B3C09F` hot-pipe candidate volumes: slots 3 (`REACTOR_MOTHER_BRAIN_HOT_PIPES_02_TRIGGER_VOLUME`), 4 (`...03...`), 5, 6 and 8 (unnamed SDK slots). Inspect actual transforms/contact surfaces. Slot 7 is a broad kill volume and must not be treated as a pipe. Current Apex rail filter uses slot 414/type60 (`SLOT_019E`, authored `reactor_rail_top_all_player_trigger` volume); applying damage across that whole space may itself be incorrect.

Native filter support: `slot_set_object_filter` has `inside` and newly added `inside_any`; `mission_effect_auth.h` validates bodies. The current union/intersection encoding passes shape tests but has not proven the intended spatial/native behavior. Inspect it before extending it further. Effects attach through type26 revision/filter authority. Keep player-only selection and remove on leaving contact.

Escape explosions have explicit authored triggers:

- `EMBER_APEX_EXPLOSION_SEQUENCE_PREFAB_EXPLOSION_SET_A_PLAYER_TRIGGER`: type31 slot105.
- B/C/D: type31 slots106/107/108.
- Corresponding type60 volumes242/243/244/245 in root80B3C21C.
- Existing scene: `EMBER_APEX_EXPLOSION_SEQUENCE_PREFAB_TRIGGERED_EXPLOSIONS_SCENE`.
- Deposit scene: `MOTHER_BRAIN_HOLE_EXPLOSION_SCENE`.

Current deposit immediately activates both scenes and arms dialogue/end triggers, not the four explosion-set triggers. Inspect whether the explosion scene owns trigger handling internally or requires explicit script-selected tracks. Arm and drive the correct authored sequence as each section is reached. Use one-shot flags; do not replay everything on backtracking. Reset only the appropriate escape state on a checkpoint retry.

Acceptance: scorch on climb pipes before deposit and correct escape fire afterward; no shock or damage outside hazard surfaces; A–D explosions follow player progress at reference timing; beam stays off after deposit.

## Existing tools and data — avoid repeating extraction

- `build/full-mission-audit/sdk.json`: generated slots, scenes, squads/anchors, directive text, trigger volumes.
- `build/full-mission-audit/descriptors.json`: slot -> config/descriptor/component mapping.
- `build/full-mission-audit/package-graph.json`: cached package graph.
- `build/sdk-mission-complete/sdk/lua/missions/mission_ember_80b3c09e.lua`: complete generated Lua mission binding.
- `build/first-encounter-audit/tags/TAG.bin` and `.meta`: extracted resources and class/size.
- `build/first-encounter-audit/game_image.bin`: native image, base `0x140000000`.
- `build/first-encounter-audit/live-*.c`: existing native decompilations. Offline IDA previously ran in an interactive tool session; do not assume its session ID is still usable.
- `/tmp/ember-xrefs.py HEX_RVA`: call/pointer references in cached native image.
- `/tmp/ember-schema.py HASH`: reflected wire schema inspection.
- `/tmp/ember-read-reactor-assets.py`, `/tmp/ember-read-final-assets.py`: standalone Proton package-reader examples. Extract only needed tags.
- Read-only capture examples: `/tmp/ember-capture-reactor.py`, `/tmp/ember-capture-mission-variables.py`, `/tmp/ember-capture-mission-observations.py`, `/tmp/ember-capture-deletion-transport.py`. Review offsets and process selection before reuse.
- `/tmp/install-ember-finale-followup.py`: latest backed-up, game-closed-checked installation example. Use a fresh backup/manifest name; preserve old evidence.

## Validation and delivery

The committed baseline built and passed 21 portable C++ tests plus five Lua suites. Current full-route test peaks: 233 durable variables / 61 intents per callback / 2 timers / 13,000 mock-instrumented instructions. Native limits: 512 variables, 64 intents, 32 timers, 100,000 Lua instructions. Do not crowd more initialization into existing callbacks.

The production sandbox removes string pattern methods (`find`, `match`, `gmatch`, `gsub`) and `dump`; a previous foundry fault resulted from using `string.find`. Tests now remove those methods during route callbacks. Use supported literal comparisons and SDK bindings.

```bash
cmake --build build -j4
cmake --build build/portable-tests -j4
ctest --test-dir build/portable-tests --output-on-failure
lua tests/mission_ember_controller_test.lua
lua tests/mission_ember_routes_test.lua build/sdk-mission-complete/sdk/lua/missions/mission_ember_80b3c09e.lua
lua tests/mission_ember_combat_ai_test.lua
lua tests/mission_ember_encounter_test.lua
lua tests/mission_ember_wipe_test.lua
git diff --check
```

Some newest tests only establish that the failed candidate emits its chosen assignments/filters; change those assertions when evidence changes the implementation. Passing tests must not be described as visual/native success.

Deliver fixes with concrete native evidence and a short manual test sequence. Install into the closed game with backup and hash checks. Ask Millie to launch; do not launch for them. Observe the Controller fight, beam cycle, climb scorch, deposit shutdown, progressive escape, stable objectives and both movies. Preserve logs and explicitly distinguish confirmed behavior from remaining hypotheses. Commit the completed work without a co-author tag.
