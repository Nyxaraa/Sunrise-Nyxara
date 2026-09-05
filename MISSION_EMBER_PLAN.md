# 1AU / mission_ember implementation plan

Prepared 2026-09-04. Scope: restore the complete mission through Lua using the installed generated SDK, with package-backed identifiers and authored resources.

## Evidence reviewed

- Reference video: https://www.youtube.com/watch?v=PqurUhqC2CE — The Videogame Library, “Destiny 2 (Xbox One) The Red War: Mission 16 - 1AU (No Commentary Longplay)”, duration 36:42. Reviewed all nine available storyboard sheets, sampling the timeline every ten seconds. This establishes broad visual order; it is not continuous playback or an audio review. Exact dialogue timing, brief interactions and damage timings remain to be verified. The recording begins in gameplay and includes subsequent story cinematics and a later mission launch; those are not all necessarily controlled by Ember.
- Generated mission: `/home/millie/Games/Sunrise/bin/x64/Sunrise/sdk/lua/missions/mission_ember_80b3c09e.lua`.
- Campaign activity: `mission_ember_38f926b2.lua`, display name `1AU`, activity index 281. Daily Heroic variant: `mission_ember_231dd291.lua`, index 54. Both point to activity root `0x80B3C07D` and scenario `0x80B3C09E`.
- SDK manifest: schema `sunrise-activity-sdk-v8`, format 37, build ID `sha256:26b80ec22191c2f87d9079ef1dc90707cf26b10e5299962304aac4e7a9e8c47a`.
- Installed packages: `/home/millie/Games/Sunrise/packages`. Read package directories and checked 154 distinct content references from the mission's slot IDs and content-tag fields: all resolved to nonempty entries across nine package families. This verifies presence, not complete semantic decoding of each payload. Runtime schema constants were excluded from the content-reference check.
- Primary family: `w64_cabal_ship_activities_019e_{0,1,2,3,5}.pkg`, with 8,192 directory entries in the latest installed directory. Root entry: 72 bytes; scenario entry: 1,773 bytes.
- Scenario SDK pack: `sdk/scenarios/80B3C09E-446ac6892dd45476cc8b91f8e3a654d92b14eaa6ed4d272f14e93cc43a927082.pack`.
- Lua references: sibling `SunriseMissions`, especially `lib/mission_lib.lua` and the regional dispatch in `tangled_shore_freeroam.lua`. Its README describes the examples as WIP, so each pattern needs checking against this branch's runtime.

## What the generated SDK already contains

| Resource | Count / finding |
| --- | --- |
| State records | 8; effective region indices 0, 1, 2, 40, 48, 49, 56, 64 |
| Named slots | 2,051 |
| Squad sensors / richer squad definitions | 162 / 31 |
| Device sensors | 69 |
| Objective sensors / directive entries | 28 / 26 |
| Player trigger sensors / trigger volumes | 127 / 211 |
| Scenes / sequence sensors / cinematic sensors | 10 / 9 / 3 |
| Dialogue cue selectors | 55; definition mappings exist for a subset |
| Task targets / performance states | Both generated tables are empty |

Specific assets include `sunburn_damage_object`, `security_placed_interceptor_object`, bridge machinery, east/west reactor clamshell devices and damage sensors, the coffin damage sensor, reactor shield, explosion scenes, escape ship and three cinematic bookend sensors. Directive descriptions explicitly identify the grinder, solar-exposure cover mechanic, bridge controls, energy stream, Interceptor, cooling vents, fusion cell and escape.

These findings support an SDK-first implementation. They do not yet prove every required runtime operation works. In particular, the difference between squad sensors and richer squad definitions must be explained before choosing how each encounter is activated. An empty task table means we cannot assume an `advance_task` target exists for every objective.

## Implementation sequence

1. **Build the mission mapping and prove the SDK bindings.**
   - Create a stage table mapping each encounter to its owning region/state, entry and exit triggers, squads, devices, objective/directive, dialogue cues, scenes and reset behavior.
   - Use stable generated identifiers. Distinguish identical display names with different slot identities. State values are all zero here: use their region-qualified identities rather than treating the values as global phase IDs.
   - Trace ambiguous mappings through the SDK catalog/scenario data and targeted package payloads. Determine whether squads absent from the richer table are scene-owned, activated through existing sensor state, or need an SDK export correction.
   - Verify the generated module, branch runtime and installed runtime agree. Exercise the existing APIs for state selection, squad placement, device transitions, triggers, directives, dialogue, cinematics and lifetime state.
   - Deliverable: an encounter map with confirmed bindings and an explicit list of unresolved operations.

2. **Implement and test the opening encounter as a complete slice.**
   - Add `scripts/mission_ember.lua` and focused modules under `scripts/mission_ember/`; include the required shared helpers in the same deployable tree.
   - Bind the campaign activity first. Resolve the correct entry state, publish its entities, activate its encounter and show its authored directive.
   - Prove an actual player trigger, a combat completion condition, the first gate transition and the following region load.
   - Handle duplicate events, death/reset and region re-entry before expanding the controller.
   - Exit criterion: launch, play the first encounter, progress, reset and replay without manual entity spawning or forced phase advancement.

3. **Implement the traversal and combat sequence in order.**

   | Section | Required behavior |
   | --- | --- |
   | Opening / mineral processing | Correct combat groups, entrances/exits, navigation, objectives and dialogue; determine any launch bookend from package ownership. |
   | Ore tunnels and grinder | Investigation and activation interaction, defend while clearing the obstruction, completion gate and onward route. |
   | Sunlit deck | Enable the authored solar hazard and cover behavior; activate the correct encounters and remove section-specific effects on exit/reset. |
   | Interior chambers and machinery | Encounter waves, moving devices, doors, bridge controls and region transitions. Confirm the exact placement of each bridge against trigger ownership. |
   | Fuel stream and Interceptor route | Energy-stream traversal, vehicle availability/boarding, enemy placement and the transition into the reactor section. |
   | Reactor sabotage | East/west vent exposure and destruction, linked objectives and scenes, subsequent core/fusion-cell interaction, shield and route changes. Confirm authored ordering and thresholds from data. |
   | Escape | Explosion sequence, navigation to the getaway ship, escape trigger, terminal mission state and the relevant cinematic handoff. |

   - Progress from actual trigger, interaction, damage, squad and scene events. Use timers where supported by authored behavior; do not copy this player's video duration into encounter gates.
   - Distinguish stage entry from stage resume. Only activate entities belonging to the appropriate loaded region.
   - Exit criterion for each section: it can be reached naturally, completed and replayed after failure.

4. **Complete presentation and mission lifecycle behavior.**
   - Pair each directive and dialogue cue with its actual event; handle alternate cues and avoid repeating one-shot lines on re-entry.
   - Restore sequences, music transitions, destruction scenes and cinematic start/termination handling using existing SDK surfaces.
   - Establish which ending scenes belong to Ember and which belong to campaign progression. Do not launch the later mission merely because it appears at the end of the video.
   - Verify completion is recorded by the supported activity/campaign path, with expected rewards and subsequent progression where applicable. A final Lua phase or a cinematic alone is not proof of completion.

5. **Make recovery and repeated play reliable.**
   - Implement the authored checkpoint/wipe behavior using the available runtime state and sensors; first confirm which state survives death, region transit and script reload.
   - Reset devices, hazards, damage targets, objectives, vehicles, encounter state and timers to the checkpoint's expected state.
   - Guard one-shot actions against duplicate/stale events. Keep regional timers and callbacks tied to the region that owns them.
   - Test backtracking, rapid trigger crossings, repeated interaction, death during animations, vehicle loss and escape failure.
   - Verify fireteam synchronization and late joins if supported by the mission's intended play mode. Test the Daily Heroic binding after campaign behavior is stable.

6. **Validate the complete mission.**
   - Check Lua loading and every referenced SDK symbol before deployment.
   - Add focused event-sequence checks for progression, duplicate events, reset and terminal completion where the available harness supports them.
   - Play from a fresh launch through the ending, with no developer intervention. Compare encounters, objectives, hazards, vehicle section and cinematics with the reference.
   - Repeat with checkpoint deaths and a fresh second launch. Review runtime logs for unresolved references, rejected operations, stale handles and missed transitions.
   - Record any remaining behavior differences explicitly. Completion requires functional gameplay and lifecycle behavior, not just reaching the final room.

## Working approach

Keep authored Lua in the `mission-ember` branch and use the installed SDK as generated input. Start with its existing capabilities. Make a targeted runtime/export fix only after a concrete required operation is shown to be unavailable or broken. Initial work should produce the encounter map and playable opening; the full mission then grows section by section using that verified approach.

No gameplay code, installed game files or runtime settings were changed during this planning pass.
