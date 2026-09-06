# Apex cooling, object initialization, and escape scorch

Changes on 2026-09-06:

- End the beam surge before publishing clamshell/coffin cooling-door opening. Keep the existing 6-second surge, 10-second exposure, and 14-second closed intervals.
- On native laser/ring object presence, reapply power and the current pose. Initial commands can precede entity creation; this is a candidate fix for the dark beam before the first surge, pending visual confirmation. Late presence after deposit must not relight the beam.
- Start the escape ship device only after its native object-presence receipt. Unlock, power on, snap to closed, then animate to open. Duplicate presence does not restart it. Authored flight playback remains to be verified in game; the script does not synthesize a flight path.
- Remove the added rail-wide thermal attachment during escape. Deposit already creates SUNBURN_DAMAGE_OBJECT, while the old script additionally attached REACTOR_COFFIN_INTERIOR_THERMAL_HOP_ON to the rails. The escape-end trigger removed only that attachment. This matches the report that damage becomes normal after reaching the end. Preserve the native sunburn object and retire the climb attachment on entering escape. No global damage multiplier is changed; exact damage rate still needs playtesting.

Package evidence correction: config 80B3D494 (ship) and 80B3D497 (sunburn) both contain fallback resource 80BFDDC2 at offset 0x538. Their actual first placed entries at offset 0x580 are respectively 80B71228 and 80B82486. Do not use the fallback model to infer ship capabilities. Ship model 80B71228 includes device component 80C70C0E and animation component 80B71226. Device placement identity 42BF7017F2901B45 matches the placed ship. Model component references have 12-byte stride, not 16.

Validation: all five mission Lua suites pass. Route peak: 235 variables, 61 intents per callback, three concurrent timers, 13,000 Lua instructions including the mock. Tests cover cooling command order, beam presence, deferred ship activation, duplicate ship receipts, and disabling the extra escape burn.

Still unresolved: exact native ending-readiness failure and full-screen surge distortion. The diagnostic DLL commit fe45d11 improves visibility but does not itself fix cinematic playback. Do not reinstate the reverted cinematic-region ordinal encoding: it caused Lime.

## Ending roster correction

The 15:52:37 stalled-run capture contains 491 allocated sync records and no type-6 cinematic record. All allocated records have byte 20 set to 1; its exact native readiness meaning is not established. This capture does not demonstrate Omega's 19-unseeded-record condition.

The roster's transitionPublication predicate still compared heldRegion directly against selectedRegion. Thus held Apex region 0 / selected bookend region 1 withheld state-local groups even after the separate arrival-window check accepted the shared world. Apply the shared-world arrival rule to the roster subset too. Both bookends may then publish while the player holds Apex; genuine world changes and unknown held-world state still defer local records.

This is isolated from the reverted global-message state-byte experiment. Global state encoding is unchanged. Release build and all 22 portable tests pass, including the production subset predicate. Native movie creation and playback need a live test; do not treat server cinematic_staged as proof of playback.
