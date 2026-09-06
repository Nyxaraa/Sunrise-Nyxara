# 1AU: movie loader, surge audio and escape explosions

## Gameplay HUD correction confirmed in game

The follow-up test of `88a9632` (delivery source commit `b9880dc`) reports reticle, ammunition, abilities, radar and mission objectives drawn over the movie. The captured log confirms `presentation_state requested=43 selected=33 applied=33 category=6` at t=766752 and native STM playing at t=767072. The selected window state is correct; it does not by itself remove gameplay HUD ownership while Apex remains loaded. Evidence is under `build/first-encounter-audit/b9880dc-hud/`.

Native window manager `1316FC0` has an independent HUD branch: it selects the `hud` name at `1317689` (or an equipment override), creates the window at `1317784`, then calls `13165C0` with role `18` at `1317796`. That setter stores the role at window offset `310`. This is different from the window-state enum at `410`.

The correction preserves cinematic state `21h` and adds a narrowly scoped draw filter in `bootflow/ember_movie_hud.cpp`. Only the full-window submission at `132C1BD` to `13D9060` can read offset `310`; nested widgets using the same draw function are passed through untouched. During owned Ember movie presentation, role 18 skips submission of the complete HUD subtree, including cached child commands. All other roles and both native UI layers continue normally, retaining subtitles and inventory/settings rendering/input. No saved preferences, visibility bits, native lists or gameplay mission state are changed. Presentation stays owned through the STM/CNN handoff and clears on final completion/failure.

The mapped-image verifier checks both unique signatures, the caller/callee relationship, root-pointer construction and the native role setter. Future regression checks should cover `hud_draw_suppressed`, subtitles, menus and restoration after leaving playback. This is a specific gameplay-window filter, not the removed blanket UI-layer mask. The release DLL built successfully and was installed after the game closed, with all 18 destination hashes verified. DLL SHA-256: `c2e84a350012ffd11eaeb39ef39bcb385b986c42dac5c0386ac0d0325f7fcd5b`; manifest `build/ember-installation.json`, backup `build/ember-install-backup-20260906-212949-077349`. No game launch or stop was performed. The user subsequently tested `3fea388` and confirmed the HUD fix “works perfectly.” This records acceptance of the reported HUD correction; individual menu/skip combinations were not separately reported.

## Correct the cinematic loading state to the playback overlay

The `41e2728` test exposed a specific presentation regression: `presentation_state requested=43 selected=34 applied=34 category=6` at t=281212, followed by valid movie playback. A read-only UI capture found window `80B46D88`, name hash `D505DEBB` = FNV-1(`loading`), above the video, alongside `80BC6681`, name hash `7737E414` = FNV-1(`subtitle_overlay`). Valid 1920x800 surfaces were captured for both movies. The previous verification established category membership, but misidentified state `0x22` as playback.

The complete native mapping is now verified:

| UI state | `1312540` window enum | `13126E0` name | Meaning |
| --- | --- | --- | --- |
| `0x21` | 26 | `17A73819` = `cinematic_overlay` | Cinematic presentation |
| `0x22` | 29 | `D505DEBB` = `loading` | Loading overlay |

Native window manager `1316FC0` reads the UI state, calls those two mappings, and explicitly excludes `0x22` from its cinematic-overlay branch at `1317094`. Selecting `0x22` was therefore instructing it to draw a loading screen. This was not a missing video frame or stalled resource request.

The bridge now substitutes `0x21` for normal gameplay while it owns ending playback. Existing loading/error/menu selections remain native. There is no drawing-layer mask or subtitle/inventory/settings allowlist; the normal UI window manager handles the transition. The native verifier now checks both complete mappings and their authored name hashes, rather than only category 4.

This same live run confirms the handoff repair: STM completed after Escape at t=443950; CNN reached playing at t=444277 and natural EOF at t=649483. Completion was accepted and orbit selection queued at t=649544; native `orbit_setup` arrived at t=653295. Those receipts establish both native movie lifecycles and orbit setup, not unobscured presentation. The new overlay selection still needs visual validation. Evidence is under `build/first-encounter-audit/41e2728-live-20260906-205738/` and `41e2728-ui-windows/`.


## Visible first movie confirmed; second-movie handoff and native presentation

The user confirmed visible STM playback with `15e507e`. Its decoder reached state 5 at t=281733, with all six selected Y/U/V definitions backed by non-null buffers. Native EOF arrived at t=481606. The bridge then stopped at `surfaces_retiring`: no completion was exposed to Lua, so CNN was never requested. This is the second-movie regression, not missing CNN media.

Read-only captures in `build/first-encounter-audit/15e507e-live-20260906-203525/`, `15e507e-after-first-surfaces.txt`, and `15e507e-after-first-buffers.txt` show that the dependent root was freed while the catalog, registrations and buffers remained resident. Root release and global asset eviction are distinct. The old bridge incorrectly made eviction a prerequisite for reporting EOF.

The correction retains one shared resource set across STM and CNN, preloading both compact media mappings. Native EOF immediately reports completion; only the same session/generation with a completed STM receipt can chain CNN onto the retained resources. The definition-only root loads first. Both movies' metadata, the shared catalog, raw buffers and registration containers load second, after validated definitions exist. Moving the catalog out of the definition root avoids retaining it during dependent cleanup. After CNN, asynchronous resource cleanup is independent of mission completion and the immediate orbit request. No asset is force-unregistered.

At the user's request, the `132BD80` UI drawing suppression and Sunrise overlay suppression have been removed. Native `E2EAE0` chooses UI state `0x22` for an active pre-rendered component: its `D8` callback is `DD1D10`, which returns false. `E2E9F0` maps `0x21` and `0x22` to cinematic category 4. The bridge now passes `0x22` to the original `E1CD60` state transition when its owned playback would otherwise select normal gameplay (`0x2B/0x2D/0x2F`). Other requests, including loading/error/menu states, pass through. Presentation persists between the movies and clears after CNN or failure. Native UI drawing and window transitions run normally. Logs include requested, selected and applied state/category.

`ember.hud.audience.<region>` is unrelated: it is a Lua once-only flag for initializing type-70 objective audiences. The intro starts `PF_CINEMATIC_BOOKEND_CINEMATIC` with `set_cinematic_active`; its authored region is not selected again for ending videos.

The new handoff guards, all 24 portable tests, full Lua route and mapped-image/package verifier pass. The Windows release DLL also builds successfully. **Visible STM is confirmed on the prior build; CNN chaining, native cinematic UI presentation and immediate orbit arrival require a new live test.** The historical cleanup-before-completion and drawing-override descriptions below describe superseded implementations.


## Freeze captured on `a47adbd`: surface registration dereferenced an unloaded definition

The final escape trigger queued STM at t=289137 and submitted its resource root at t=289153. No playback submission followed. All six containers were resident, but all six definitions were unloaded (`FEFE0000 / type_info 001044FB / blob 0`). Scanning the frozen process found the render thread's saved exception: **C0000005 at game+1204163, RAX=0, RBX=80BCA021, RDX=4**. That instruction reads the definition's slot byte during container registration. This establishes the fault directly; neither decoding nor orbit return had begun.

Evidence is preserved in `build/first-encounter-audit/a47adbd-surface-exception.txt`, `a47adbd-frozen-surfaces.txt`, `a47adbd-fiber/surface-exception-stack.bin`, and the `a47adbd-live-20260906-201858` log directory. The process was inspected without launching, stopping or modifying the game.

The corrected loader uses two asynchronous roots. The first retains metadata, the compact movie stream mapping and the six definitions. Only after native completion and validation of each 16-byte type-19 definition, slot and format does it submit the second root containing the six raw backing buffers and six registration containers. Merely inserting definitions earlier in the same unordered batch would not establish this dependency.

The backing buffer tags are `80BCA020`, `80BCA023`, `80BCA027`, `80BCA02A`, `80BCA02D`, and `80BCA030`. Their package type is `254FB`, paired with definition type `44FB`; both use native request kind 1. Native `1202430` expands the eight-byte definition to 16 bytes. The type-19 buffer callback at `1204581` fills definition+8, which `1184660` reads through `4A6340`. Playback now requires all six non-null backing pointers as well as the matching registrations and published selections.

Cleanup releases the dependent root first and waits for the native container registrations, selections and backing records to disappear before releasing the definitions. Completion is not exposed to Lua until this cleanup finishes, preventing the next movie from being refused while the previous resources are retained. Once native EOF has been observed, cleanup no longer reads the released decoder. The immediate return-to-orbit selection remains after movie two completes; there is no added banner delay.

The captured free-list/invalid-definition cases and outstanding-registration cleanup cases pass regression tests. All 24 portable tests and the native/package ABI verifier pass. Visible video, hidden HUD, both movie transitions and the orbit arrival still require a fresh live run of this correction.

## Correction after the active black-picture trace

The manual replay established the failure directly: valid 1920×800 Y/U/V frames were present in native CPU buffers, and the UI command stream included the native movie command (`1B`, with packed header flags). A decoded frame from `picture-20260906-194230/` visibly contains the cutscene. The six GPU video surface slots were null during playback. Their native registration rows had **count 0 / selected FFFFFFFF**, with only stale candidate values. The prior raw metadata request did not retain the six surface containers named by `80BCA032`.

The resource root now explicitly owns those six ordinary, kind-1 surface containers alongside the metadata and compact stream mapping:

| Slot | Plane | Container | Definition |
|---|---|---|---|
| 1 | Y0 | 80BCA022 | 80BCA021 |
| 2 | Y1 | 80BCA025 | 80BCA024 |
| 3 | U0 | 80BCA028 | 80BCA026 |
| 4 | U1 | 80BCA02B | 80BCA029 |
| 5 | V0 | 80BCA02E | 80BCA02C |
| 6 | V1 | 80BCA031 | 80BCA02F |

Each container is class `80806B91`, containing its single definition reference. Each definition's first byte identifies the matching native slot. Once the root is ready and the player is idle, the bridge verifies all six retained registrations, then invokes native `1202B00`, the publication operation normally called at world activation `B5F4CE`. Stale candidates with count zero, invalid counts, other surface owners, or unrelated pending changes to slots 0/7 cannot pass. Playback waits for matching published selections; it never writes GPU pointers or bypasses the renderer's allocation checks. The root remains held until native movie completion releases it.

Native `132B890` queues the movie and letterbox before calling the two UI drawing layers at `132BD80`. The new scoped hook suppresses those gameplay UI layers only while the Ember bridge owns playback. UI state and native movie rendering remain intact, and drawing returns at completion/failure. Sunrise's own debug/interaction overlays are also suppressed during this window. Native audio, both movies' order, completion receipts, and Escape handling are unchanged.

The release build, all 24 portable tests, restored full Lua route, package surface references, native publication ABI and UI call order pass offline verification. **The installed correction still needs visible-video/HUD verification in game.** The replay confirmed native EOF for STM at t=456666 and automatic CNN playback at t=457139. Capture files are under `build/first-encounter-audit/picture-20260906-194*`, `live-picture-resources.txt`, and `live-surface-rows.txt`. The capture helper was stopped; the game was never launched or stopped by the agent.

## Playback report after `6540584`

The user confirms immediate ending audio, but the picture stays black with gameplay HUD visible throughout. Both native players reached decoder state 5. STM subsequently stopped and completed after Escape (t=384298–384404); CNN also reached state 5 (t=384750) and completed after Escape (t=385250–385356). This establishes decoding/playback state and skip sequencing, **not visible video**. The archived log is `build/first-encounter-audit/6540584-black-video.log`.

The failed beam inversion from `6540584` has been reverted at the user's request: normal uses `open`, surge uses `close`. Audio remains tied to the surge callback; shutter mechanics are unchanged. The restored mission route and all 24 portable tests pass.

The picture investigation separates CPU extraction (`41D140`), fullscreen UI command production (`132B890` → `1278FF0`, command `1B`), GPU Y/U/V upload (`41D7B0`), and the final video draw (`1159CE0`, global shader index `B4`). Shader `80B35981` is resident in the captured process. The idle UI manager at `(RIP target of 132BD5C) & ~15 = 142F4EE30` has the expected `+1E4 = -1`, `+1EC = 0`. Those initial post-skip readings did not identify the failure; the later active trace above does.


## Latest playtest: metadata ready, stream missing

The user confirms `13f07ee` no longer freezes, but neither ending plays. The log records movie 1 resources ready and playback submitted at t=256404, decoder state 1 at t=256487, state 7 at t=256527, then failure and resource release at t=256598. The escape trigger and movie dispatch are working; the native decoder never reaches playing state 5.

A read-only capture after the failure found stream tag `80BCA034` still in a free-list state: datum `FEFE0035 / type_info 0`, decoding to location 0. The movie headers were resident. Native video I/O `41A160` consumes the media datum through `3597C0` (open package/patch) and `357DA0` (file offset and length). The previous bridge explicitly requested four metadata records but omitted this stream mapping.

The native load job `3591B0`, branch `3592C6`, handles `(type_info & 30000) == 10000` by publishing **offset | patch id** and **length | C0000000** with `351D00`, then returning before the ordinary allocation/read branch. Requesting the media tag initializes this compact mapping; it does **not** allocate a buffer for the full movie. The prior assumption that omitting this request was needed to preserve streaming was wrong.

Both movies now include their exact media tag as a fifth kind-1 root request: STM `80BCA034`, CNN `80C7C000`. Playback additionally requires the video-family/type bits, an encoded nonzero length and valid package location. The mapping is never dereferenced as a CPU pointer. The resource root holds it until native playback releases its reference. Tests cover the captured free-list datum, wrong types, empty lengths and invalid locations, and the native/package check verifies this stream initialization branch and both authored media entries.

The user's same test establishes the opposite beam pose mapping from the prior implementation: **close = normal, open = surge**. Both beam devices now follow that mapping at entry, warning, cooling and shutdown. The encounter clock and alarm's surge callback remain unchanged.

Evidence: `build/first-encounter-audit/13f07ee-decoder-failure.log`. The latest change is compiled and checked offline; successful rendered playback is still awaiting the next run.

## Previous correction and validation

The latest changes address the captured `bc3e912` movie-loader crash, couple the alarm request to the beam surge, and forward escape triggers into the authored explosion scene. Release compilation, 24 portable tests, all five Lua mission suites, native ABI/package checks, and scene schema/content checks pass. **Rendered ending playback, audible alignment and visible explosions still require a fresh game test.** Offline verification does not establish those outcomes.

## The freeze: two distinct failures

`4f5c706` called the movie player before requesting its assets. The decoder worker faulted at `game+349D2C`, reading address 8; movie wrappers and headers contained `FEFE` free-list entries. These TagHashes are valid runtime handles, but their contents were not resident.

`bc3e912` added asynchronous resource loading, but copied request kind **2** from startup loader `B46E10`. That kind is specific to shared/type-16 resources. The next captured worker exception was **C0000005 at `game+3374C6`, reading address 8**. Its request record at `8F267A0` contained movie `80BCA001`, unresolved shared-resource handle `FFFFFFFF`, and kind 2. The resource root was pending; acquire/play had not yet been called. This is the direct cause of this test's freeze, not cinematic authority waiting on a missing global seed.

Native `426920` derives the correct request kind from each package entry: kind 2 only when `(type_info & F000) == 2000`, otherwise kind 1. `4312D0` routes those kinds to different root lists. The installed movie records are ordinary tags:

| Asset | Class | type_info | Required kind |
| --- | --- | --- | --- |
| `80BCA001`, `80BCA003` movie wrappers | `80808495` | `100A` | 1 |
| `80BCA000`, `80BCA002` movie headers | `80808499` | `103B` | 1 |
| `80B9EB33`, `80B9EB34` subtitle metadata | `80809A88` | `1019` | 1 |
| `80BCA032` shared movie metadata | `80806B8F` | `103B` | 1 |

The correction creates one native asynchronous root and adds the selected wrapper, its header, its subtitle metadata and `80BCA032`, each using `{1, tag}`. It retains that root through playback. The media mapping must also be included, as corrected above; its loader path preserves native streaming.

Playback requires native root state 2, matching resident wrapper/header classes, the expected subtitle reference and resident metadata, and a present media reference. Only then call the original acquire/play pair. Both native completion and Escape stopping must finish before releasing the root. Pending roots are polled; the frame never invokes a global I/O drain. Failed or timed-out preparation never completes the mission.

The established sequence remains **escape trigger -> STM -> CNN -> mission complete**, using exact decoder asset/state and native busy completion. Apex stays loaded during the movies; this avoids the separately observed world-retirement failure. See [the native movie bridge](mission-ember-prerendered-ending.md) for its lifecycle and frame observer.

## Beam audio follows the surge request

`beam_surge(c, s, true)` now changes the beam drive and queues the appropriate surviving clamshell/coffin alarm sequences in the same callback. Its rising-edge guard prevents repeated alarms from redundant updates. Cooling changes the drive back without replaying an alarm.

The separate `surge_audio` timer and its speculative preroll offsets are removed. The visual/mechanical cycle stays 14 seconds closed, 6 seconds surge, 10 seconds cooling; shutters still open after the surge. This establishes shared script timing, not a measured sample-accurate native sound onset. Actual audible alignment needs confirmation in game.

## Escape explosions need retained scene inputs

The failed live run already reported all four authored player triggers: registry `A3B76C64`, slots **105..108**, volumes **242..245**, at t=297197, 302773, 306890 and 312164. The missing step was forwarding them. Generic `scene:activate{}` only publishes a generation with an empty external event list; it does not automatically connect those trigger receipts to the scene.

The native scene is `A3B76C64/43/20`, object `80B3C21C`, config `80B3C0BA+368`, resource entity `80B8248D`, graph `80BEB1CC`. Its external event-gate nodes, class `8080637D`, contain these exact FNV-1 keys:

| Player trigger | Authored event name | Event key | Graph key offset |
| --- | --- | --- | --- |
| A / slot 105 | `explosion_set_a_trigger` | `329EB106` | `5BC0` |
| B / slot 106 | `explosion_set_b_trigger` | `633B82E9` | `5C20` |
| C / slot 107 | `explosion_set_c_trigger` | `15A78938` | `5C80` |
| D / slot 108 | `explosion_set_d_trigger` | `F9D55A83` | `5CE0` |

New Lua slot method:

```lua
scene:set_scene_events{generation = 1, events = {0x329EB106, 0x633B82E9}}
```

It accepts only an exact type-43 / component `80806382` / schema `8080626B` SDK binding. The packet contains signed generation, stop=false, zero dependencies, scalar=0, event count and up to 32 unique nonzero event keys: **74 + 32*n bits**. The runtime validates this restricted schema before transport, including lengths, duplicates, forbidden fields and padding. This is a full scene Auth replacement, not a squad/combatant field patch.

Deposit starts one generation with an empty list. Each phase-6 route trigger adds its corresponding key to the retained cumulative list without changing generation. Duplicate receipts and backtracking add nothing. Escape checkpoint reset increments generation and clears history. This matches the retained-event mechanism documented for native `B41330` in `MissionDocs/IKORA-ANIMATION-AND-ENDING.md`: unchanged generations and already committed keys do not restart or refire the graph. The native scene owns the explosion timing, placements, animation and effects. The separate core-hole explosion on deposit remains independent.

## Existing scorch ownership

This patch does not change damage. The prior single-owner path remains: Ember's exact slot-43 attachment source temporarily substitutes resident authored sunburn entity `80B82489`; the same source moves its filter from climb pipes to escape rails on deposit. `SUNBURN_DAMAGE_OBJECT` stays disabled to avoid a second damage owner. Native attachment registration, duplicate checks and removal remain in control. The prior test did not establish final damage-rate correctness.

## Reproduction and validation

Evidence lives under `build/first-encounter-audit/`: `bc3e912-resource-stall.log`, `movie-stacks-20260906-184021/`, and the resource/request captures made while that process was frozen. The prior decoder-residency failure is in `direct-movie-stall-4f5c706-20260906-1803.log` and `movie-stacks-20260906-180532/`.

Checks run:

```sh
cmake --build build -j4
cmake --build build/portable-tests -j4
ctest --test-dir build/portable-tests --output-on-failure
python3 tests/verify_ember_movie_native.py build/first-encounter-audit/game_image.bin /home/millie/Games/Sunrise/packages
python3 tests/verify_ember_explosion_content.py build/sdk-corrected/activity_sdk.pack build/first-encounter-audit/tags/80BEB1CC.bin
lua tests/mission_ember_controller_test.lua
lua tests/mission_ember_encounter_test.lua
lua tests/mission_ember_wipe_test.lua
lua tests/mission_ember_combat_ai_test.lua
lua tests/mission_ember_routes_test.lua build/sdk-mission-complete/sdk/lua/missions/mission_ember_80b3c09e.lua
```

The full route peaks at 244 durable variables, 61 intents per event, three timers, and 13,000 Lua instructions including the test mock. Packet tests check an independently assembled four-event fixture, empty/reset and maximum lists, invalid generations, malformed fields, duplicates, truncation and padding. Native verification now checks the request-kind branch and installed metadata classes/types, in addition to function addresses and call offsets. Authored-content verification checks the actual SDK wire schema and four event hashes in the extracted graph.

Game acceptance: beam alarm accompanies surge without changing shutter timing; explosions A-D play as the player passes their volumes; a wipe allows a new traversal; STM has moving frames and audio, then CNN plays, and only its completion completes the mission. Test normal EOF and both Escape skips. Expected movie diagnostics: `resource_requested ... kind=1 metadata=4`, `resource_ready`, `submitted`, `playing`, `complete`, and resource release, repeated for movie 2. A request or a ready resource alone is not success.
