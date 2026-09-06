# Ember's pre-rendered ending

## Why the world transition was wrong for this path

The a8d3064 live test removed all eight Apex groups, then froze in `network_send` immediately after teleporting to bookend region 1. No movie controller appeared. This is a remaining teardown defect, not a movie playback failure. The direct path below avoids that transition; it does not repair the general teardown defect.

The two Ember bookends differ from the Omega in-engine cinematic:

| Bookend | Placement | Entity | Component | Movie asset |
| --- | --- | --- | --- | --- |
| STM (first) | 80B3C222 | 80B38179 | 80BDDC62 | 80BCA001 |
| CNN (second) | 80B3C226 | 80B3817B | 80BDDC67 | 80BCA003 |

The placement's entity reference is at +0x30. Both entities contain class 808065EB, configured by 808065EC. The component config begins at +0xC8; its movie identifier is at config+0x4C (file+0x114). The mapped native class table at 1CDB480 identifies the corresponding component operations. These are package/native-code observations, not inferred from the slot names.

## Required residency correction

The installed `4f5c706` bridge froze on its first live STM request: the resource lookup read address 0x8 at `349D2C`. Both movie wrappers and headers were registered but unloaded. The subsequent `bc3e912` loader also failed: its kind-2 request sent the ordinary movie tag down the shared-resource path, leaving a handle at `FFFFFFFF` before a worker dereferenced it at `3374C6`. The current bridge uses the package-verified kind 1 and pins the movie, header, subtitle and shared metadata tags plus the compact video-stream mapping before playback. The `13f07ee` test reached decoder preparation but failed with state 7; it had omitted that stream mapping. Native `3591B0` initializes the media datum without copying the full video into RAM. See [the correction evidence and validation](mission-ember-final-corrections.md). Rendered playback remains unconfirmed until a fresh game test.

## Native playback bridge

Native pre-rendered component start DDB0F0 acquires the movie manager with 41B040 / 41A3C0, then calls 41CD20(manager, movieAsset, 0). Stop DDB830 calls 41D0C0 and balances its acquire with 41A980. The new bridge follows that acquire/play/stop/release contract for exactly the two assets above. It does not instantiate a dummy cinematic controller or manufacture type-6 incidents.

The API is resolved through unique executable signatures on the component operations, and verified relative call targets; no fixed address is used for executable calls. Offline matching against the saved game image resolved start DDB0F0, stop DDB830, busy predicate 41B420, manager accessor 41B040, decoder accessor 41AB70 and movie frame 41D140.

Movie requests are ordinary committed mission intents, limited to private mission_ember in Apex. Lua uses `context:play_prerendered_movie{index=1|2}` and reads `context:prerendered_movie_status(index)`. Each native request carries session, ActivityClient generation and request identity. Startup waits for an idle native player; another movie is never replaced. Calls execute on the game frame, not the network delivery thread.

The decoder's current asset is +0x1B4 and native state is +0x1B0. Rendering in 41D140 is gated on state 5. The bridge requires state 5 for its exact asset before reporting playback, then requires the native busy predicate to clear with stopped/end state 0 or 6 before completion. An error, replaced decoder, changed asset after playback, or changed world fails without completing the mission. Queue/preparation have 30-second bounds; playback has a 600-second bound. These deadlines fail, never award completion.

The movie frame is observed after its original routine, as well as the player-camera frame. This is necessary because movie presentation may stop the player-camera callback. The extra frame poll is active only while this bridge has a request. Foreground Escape invokes the same native stop operation, edge-triggered only during confirmed playback. Direct videos have no type-6 source to emit the existing cinematic-skip incident, so unrelated/stale type-6 incidents are ignored. Completion still waits for the native stopped receipt.

## Mission ordering

Escape disables scorch and starts STM without selecting another mission state. Apex remains loaded while the native player owns video presentation. A bounded Lua timer reads playback status every 250 ms, so advancing the movies does not depend on a new client-state delta. Confirmed STM completion queues CNN. Only confirmed CNN completion sets `ember.complete`, phase 100, and native lifetime state 6. Gameplay route callbacks remain gated while the ending is active.

This removes the failing transition from the ending route. General world teardown, the old type-6 bookend transfer code, and Omega's in-engine resource readiness rules are not claimed fixed by this change.

## Validation

All 23 portable tests and five Lua mission suites pass. Coverage rejects queue/preparation as completion, wrong assets, decoder errors, stale type-6 incidents, duplicate completion, and mission completion after only the first movie. It also checks that the ending requests no world selection and that native completion following a skip follows the same sequence. All four native signatures and six relative-call targets match the saved executable image.

Live playback, video/audio presentation, both Escape skips and the post-movie mission-complete presentation still require a game test. Diagnostics use `ev=ember_movie` with queued, submitted, decoder, playing, stop_requested and complete, or a specific failure reason.
