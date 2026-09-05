# Ember implementation status

## Landing spawn selection — 2026-09-05

The next playtest confirmed the movie completes and the player spawns, but in the wrong place
under the map. Logs now resolve both cinematic notifications and arm the transition to state 64.
The installed arrival override selected the cinematic region without an explicit spawn set.

The native build-data catalogue contains a three-point landing set whose hash matches
`powerhouse_landing_mercury` (`0x9C58857A`). Its points align with the SDK landing anchors and
the opening area in the reference video. Settings now explicitly select that set while retaining
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
