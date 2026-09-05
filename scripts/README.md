# Ember development scripts

`mission_ember.lua` currently implements only the opening landing combat slice. It is not a
complete mission controller. The initial six-squad selection needs an in-game playtest. Bridge
interaction, later encounters, authored checkpoints, dialogue and mission completion remain to
be implemented.

The original installed SDK inspected on 2026-09-04 lacks the opening squad definitions. This controller
intentionally reports the missing binding before publishing any gameplay state. Build this branch
and regenerate the SDK first; the scenario-scoping fix in the squad linker addresses object-key
collisions between campaign and arcade content. Generation must still verify runnable member
counts, actor bindings and anchors. The isolated SDK generated in `build/sdk-generated` now passes
these checks for the initial six opening squads. Do not remove checks or supply replacement enemies.

After regeneration, copy this directory's contents to `Sunrise/scripts` beside the generated
`Sunrise/sdk/lua` directory in the game artifact tree. The runtime selects `mission_ember.lua`
by the activity name. Enable the existing `server.activation.mission_scripting` setting.

The controller declares region 64 at startup and waits for the client to hold it before spawning enemies. Enemy clear
requires evidence that every watched squad existed and now has no live members. Duplicate clear
events do not advance twice, and removed squads must be observed alive again. Region transit and
script reload do not reset the encounter. `Encounter:reset` is available for a future confirmed
checkpoint callback; this version does not infer a wipe from region transit.

Fresh launches declare the opening powerhouse state through `initial_state`. The installed
`state.activity.arrival_overrides` must also include `mission_ember` with `bubble: 8` and
`slice_set: 64`; the arrival router otherwise chooses the first live bubble (the reactor area).
The branch's bundled defaults include this override. Existing settings need the row added
explicitly. Restart the game after changing arrival settings.

The combat-clear callback displays the authored bridge-control directive. It does not move the
bridge, complete the activity or silently proceed through unimplemented encounters.

Run local checks from the repository root:

```sh
lua5.4 tests/mission_ember_encounter_test.lua
lua5.4 tests/mission_ember_controller_test.lua
python -m unittest discover -s tests -p 'test_*.py'
g++ -std=c++23 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    tests/squad_reference_scope_test.cpp -lcrypto -o /tmp/ember-squad-reference-test
ASAN_OPTIONS=detect_leaks=0 /tmp/ember-squad-reference-test
```

Leak detection is disabled in this command because it cannot run under the sandbox's tracing;
address and undefined-behavior instrumentation remain enabled. The C++ test uses synthetic
identities and exercises the production reference resolver. Lua tests model the context API;
they do not substitute for a live mission run.

Generate a local SDK inventory without committing package data:

```sh
python tools/inspect_mission_sdk.py /path/to/Sunrise/activity_sdk.pack \
    --shard /path/to/Sunrise/sdk/scenarios/80B3C09E-HASH.pack \
    --output /tmp/mission-ember-inventory.json
```

The current landing test places ten SDK squads (the original six and the four available
bonus-support squads), using their authored default counts. Bonus-wave timing remains unverified;
the missing bonus-anchor and far-side definitions are not substituted. Startup snaps the six
bridge devices closed and resets the bridge objective after the initial state is published.
The region callback no longer redundantly selects that state. Bridge interaction and extension
still need scripting; this test checks its initial state only. Server/client info logging is
installed to expose refused spawn and device requests on the next fresh launch.
