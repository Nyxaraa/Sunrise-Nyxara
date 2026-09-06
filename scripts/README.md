# Ember mission scripts

The controller now covers arrival, Powerhouse, Mineral Processing, Sunside, Light's End,
reactor destruction, fusion-cell overload, escape and both authored ending cinematics.
This is a reconstructed full-mission candidate. The user has verified the Powerhouse
combat, bridge, Harvester doors and exit; the later encounters are tested offline and need
an in-game playthrough.

Use the matching native DLL and generated SDK together. The final SDK in
`build/sdk-mission-complete` resolves all162 squad sensors to173 runnable definitions;
11 Processing sensors have two distinct valid authored rule bindings. The Lua roster
chooses each sensor once and uses native member counts, placements, combat objectives
and task costs. No alternate enemy types or hand-entered spawn positions replace them.

`mission_ember.lua` dispatches the opening/landing and later controllers. Later modules
are `processing.lua`, `cinder.lua` and `apex.lua`, with shared `route_roster.lua`,
`route_support.lua`, `carry.lua`, `routes.lua` and `ending.lua`. Progress is transactional
mission state, and completed encounters retain zero native spawn requests on backtracking.

Navigation uses authored type47 marker references in type68 directives, hidden while the
relevant encounter is active. Darkness uses the native3/30-second respawn policy and
all-dead wipe handshake. Checkpoint reset dispatches to the active encounter; actor,
interaction and damage generations reject stale receipts. Escape and native cinematic
skip incidents advance the ending only once.

Enable `server.activation.mission_scripting` and SDK generation/Lua declarations in
settings. Install all modules under `Sunrise/scripts`, beside `Sunrise/sdk/lua`, with the
matching `Sunrise/activity_sdk.pack`. The installed settings already select cinematic
arrival; preserve them. The user launches the game manually.

See [implementation plan](../MISSION_EMBER_PLAN.md),
[status and validation](../MISSION_EMBER_STATUS.md), and
[reference observations](../MISSION_EMBER_REFERENCE.md) for the candidate's limits.
The build is not a claim of verified retail-equivalent timing or complete hazard behavior.
