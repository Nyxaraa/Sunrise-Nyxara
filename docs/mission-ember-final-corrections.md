# 1AU: final playback, burn and surge correction

## Confirmed failure and plan

The `4f5c706` playtest reached escape, queued STM at t=284067 and submitted it at t=284084. The decoder stayed at state 0. The deeper worker stack contains an exception record: `C0000005`, instruction `game+349D2C`, reading address `0x8`. Its callers are `41A810 -> 41CB60 -> 41D590` (movie resource lookup, prepare, manager service). This is an access violation during preparation, not an ordinary slow load and not the earlier world-retirement failure.

Read-only residency capture confirms the cause: movie tags `80BCA001/000/003/002` still contain `FEFE` free-list entries. The package is registered, but these entries were never requested. A package TagHash is already a valid runtime handle; the missing operation is **loading**, not converting the numeric hash. The package loading and handle guides under Sunrise-docs/refs document that distinction.

Implement and verify the following:

1. Use the native asynchronous resource-root producer to load each movie and its authored dependency graph. Require the completed root and matching resident wrapper/header before calling the player. Keep the resource request through playback, then release it without a global I/O drain.
2. Keep STM -> CNN -> mission complete driven by real playback and completion observations. Retain Apex while the videos play, avoiding the independently broken world teardown. Failures must leave the mission incomplete and report a specific cause.
3. Use one native sunburn effect attachment for the climb and escape. Change its filter at deposit; disable the separate damage-volume object. Preserve native damage timing, duplicate protection and detachment instead of applying an additional damage multiplier.
4. Advance only the surge sequence request by four seconds. Keep the visual/mechanical clock at 14 seconds closed, 6 seconds surge, 10 seconds cooling.
5. Build, run native ABI checks and mission regression suites, then install with backups and hashes while the game is closed. The user launches the game.

## Executed implementation

### Native movie resource lifetime

The bridge follows the existing startup loader `B46E10`:

- `4294D0()` gets the resource manager.
- `423EF0(manager, &root, 8, 2, 0, "mission_ember_movie")` creates an asynchronous root.
- `4312D0(rootObject, {2, movieTag})` adds the authored resource.
- `435AA0(manager, rootHandle)` submits the request.
- `42C650(rootObject)` observes native root state: 1 pending, 2 completed, 3 failed.
- The wrapper must be class `80808495`; its +8 child must be the exact corresponding class `80808499` movie header; the media reference must be present.
- Only then run the existing acquire/play pair `41A3C0 / 41CD20`.
- After the native movie player releases its reference, `425310` disposes the completed request. Pending requests are polled, never synchronously drained on the frame. Failed requests do not award completion.

All callable addresses are resolved through unique native signatures and relative call targets. No hardcoded executable address is called. The saved executable verifies the six resource calls and pool accessor. The existing decoder rules still require state 5 for the exact asset, then native completion; queued and prepared requests cannot complete a movie.

### Single sunburn attachment

The placed native sunburn volume's condition component (`80B82485`, `c_condition_vol_component_*`) references effect entity `80B82489`. That entity carries its own burn logic and visual components (`80F7AB1E` and `80BEB1C9`). The scripted deck heat shimmer (`80B3A2A8`) is only the visual layer; attaching it alone would not supply burn damage.

A read-only live capture verifies Ember slot 43's source `(80B3C0C6, 80809540, +AC8)` and its writable attachment template: `self+210+relative`, with resource `80C1D9E0` at +0. Native `9F2760 -> 56DE00` consumes that template to create an actor's tracked child attachment.

The narrowly scoped hook substitutes resident `80B82489` only during that one source's native attachment call, then restores the template even on exception. It does not modify package data or Foundry's shared resource. Native duplicate checks, attachment registration and removal remain in control. A missing sunburn resource refuses attachment and logs it rather than dereferencing unloaded data.

Lua uses that same slot for the five climb-pipe volumes and, after deposit, the escape rail volume `60/414`. It moves the filter with a new revision, never enables a second attachment owner, and keeps `SUNBURN_DAMAGE_OBJECT` disabled. Completion and checkpoint handling clear/reconcile the attachment through the existing hazard lifecycle. Damage rate and visual behavior still need confirmation in the game; the source/ownership fix is established, not a measured final health-loss rate.

### Audio

The sequence request changes from 4000 ms into the closed window to the next timer event (1 ms). This is approximately four seconds earlier. No beam pose, surge, shutter, or cooling duration changes.

## Failure cases checked

| Case | Required behavior |
| --- | --- |
| Registered but unloaded movie | Request and wait; never call native playback |
| Partially resident or wrong movie header | Keep waiting, then fail on the preparation bound |
| Native resource load fails | Report failure; do not complete mission |
| Preparation timeout with pending I/O | Leave frame responsive; defer disposal until request finishes |
| Decoder never reaches playback | Do not count it as a completed movie |
| Wrong decoder/asset, world change | Fail rather than complete another movie's request |
| Repeated escape, completion, or hazard callbacks | No duplicate movie completion or burn attachment |
| Deposit | Stop beam; move the existing burn to escape bounds; no volume-object damage in parallel |
| Movie EOF or player skip | Confirm native stop before starting the next movie / completing mission |
| Another mission or another effect source | Sunburn substitution does not match |

## Validation and remaining live test

Release build, 23 portable tests, all five Lua mission suites, and `tests/verify_ember_movie_native.py` pass. The full route stays within 239 variables, 61 intents/event and four timers. The native test validates signatures, call offsets, table accessor and attachment asset field against the mapped image. The portable cases explicitly reject unloaded/partial/wrong headers and pending disposal, and verify burn source isolation. Lua tests cover single-source damage ownership and the revised audio request timing.

The fix has **not yet been confirmed by a fresh game run**. Acceptance requires: scorch after deposit with normal damage and clean removal; surge audio aligned with the existing visual; STM begins at escape with moving frames/audio; CNN follows; mission completion occurs only after CNN; both movies can be skipped; no freeze. Expected diagnostics are `resource_requested -> resource_ready -> submitted -> playing -> complete -> resource_released` (release may log immediately before complete), repeated for movie 2. Burn attaches log `ev=ember_sunburn result=attached ... asset=80B82489`.

Evidence is saved under `build/first-encounter-audit/`: `direct-movie-stall-4f5c706-20260906-1803.log`, `movie-stacks-20260906-180532/00000180.bin`, `direct-movie-20260906-180404/manager.bin`, and `reactor-runtime-20260906-181944/`. These are diagnostics, not a promise that passing offline checks proves rendered playback.
