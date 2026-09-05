# Ember development scripts

This controller is a development playtest slice, not complete 1AU. It selects the authored
arrival-cinematic state (49), activates its bookend when held, and hands off to powerhouse
state 64 on the matching termination event. The separate arrival reference establishes the
movie sequence, and the latest playtest confirmed playback and the termination handoff.

The landing encounter uses eleven authored Mercury squads with their default counts.
Bridge devices reset only when the playable region is held. Arrival dialogue waits for spawn
settlement; combat clear sets the console directive and plays the console-guidance cue.
Actual Ghost-console activation/bridge extension, later encounters, later dialogue, wipes and
mission completion still need implementation and in-game verification. See
[the full squad audit](../MISSION_EMBER_SQUADS.md) and
[video observations](../MISSION_EMBER_REFERENCE.md).

Build this branch and regenerate the SDK before installing these scripts. The corrected
native generator initializes the scenario catalogue before offline extraction and rejects
cached shards missing container/spatial context. `build/sdk-corrected` has 138 statically
runnable Ember definitions; 13 sensors have no definition and 11 processing sensors have
refused alternate spawn-rule bindings. Never substitute arbitrary enemies or coordinates.

Copy these scripts to `Sunrise/scripts` beside `Sunrise/sdk/lua`. Enable
`server.activation.mission_scripting`. For cinematic arrival, the settings override for
`mission_ember` must use `bubble: 6`, `slice_set: 49` and
`spawn_set_hash: "0x9C58857A"` (the authored Mercury landing set). Gameplay is selected by
the script after the cinematic. Use the current native build so the explicit spawn override
is preserved even when its package is absent from the direct scenario package list.
Restart after updating arrival settings.

Region-less client deltas preserve encounter ownership. Duplicate events and script reload
do not repeat the movie, spawn requests, dialogue or bridge reset. Mock-context tests verify
those conditions; they do not establish visible AI, dialogue or cinematic playback.

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
