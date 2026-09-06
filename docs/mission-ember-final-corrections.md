# 1AU: movie loader, surge audio and escape explosions

## Playback report after `6540584`

The user confirms immediate ending audio, but the picture stays black with gameplay HUD visible throughout. Both native players reached decoder state 5. STM subsequently stopped and completed after Escape (t=384298–384404); CNN also reached state 5 (t=384750) and completed after Escape (t=385250–385356). This establishes decoding/playback state and skip sequencing, **not visible video**. The archived log is `build/first-encounter-audit/6540584-black-video.log`.

The failed beam inversion from `6540584` has been reverted at the user's request: normal uses `open`, surge uses `close`. Audio remains tied to the surge callback; shutter mechanics are unchanged. The restored mission route and all 24 portable tests pass.

The picture investigation separates CPU extraction (`41D140`), fullscreen UI command production (`132B890` → `1278FF0`, command `1B`), GPU Y/U/V upload (`41D7B0`), and the final video draw (`1159CE0`, global shader index `B4`). Shader `80B35981` is resident in the captured process. The idle UI manager at `(RIP target of 132BD5C) & ~15 = 142F4EE30` has the expected `+1E4 = -1`, `+1EC = 0`. These **post-skip** readings do not identify the during-playback failure; an active picture/command-buffer capture is pending.


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
