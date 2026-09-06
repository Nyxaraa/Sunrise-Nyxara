# Post-Opus follow-up — 6 September 2026

Read `OpusHandoff.md` and retained Claude's latest beam polarity (normal open, surge close), thermal effect, objective and ending changes. This is an installed test candidate, not a claim that the remaining visual issues have passed gameplay validation.

## Loading cinematic suppression

The Omega contributor supplied `LoadingCinematics_Suppressed`, then RVA `0xC24490`. In the captured native image this is a no-argument boolean wrapper. Its travel/spaceflight callers skip cinematic setup and waiting when it returns true (D46900, D474D0, EE50A0 and EF44C0 inspected).

`client.suppress_loading_cinematics` now forces that predicate true only when the native loading flow names 1AU (definition 38F926B2). It defaults false in source and is enabled in the installed settings. Setting it false restores the original predicate. The scope is re-evaluated on every call through the same native activity reader the loading flow uses (C294B0). The SDK maps its index to the definition hash. Orbit, another activity, an absent SDK or an unresolved activity uses the original predicate; retained host sessions do not control suppression. The scripted type-6 movie controller is not detoured. Verify that the erroneous Earth fly-in disappears while the authored 1AU introduction and ending movies still play.

The hook resolves the unique EF44C0 caller signature, follows its call at +30 and checks the target's boolean-wrapper bytes. Offline signature validation found exactly one caller and resolved C24490. The short predicate signature alone matches two functions and is deliberately not used for lookup.

## Ending publication

Claude's pending fix cleared an arrival window when the old and new selected plans share a slice set. Ordinary traversal can leave the selected plan at landing (64) while the client already holds Apex (0). Selecting bookend 1 then incorrectly reopened an arrival window by comparing plans alone.

Selection now reads the instantiated client region. A target world already held does not open an arrival wait, and a repeated selection repairs a stale pending flag. Tests cover landing-plan-to-bookend selection with Apex held, a sibling bookend, a real world change, and unknown client placement. A live ending test must verify publication advances and both STM/CNN movies complete; this fix addresses the observed lease deadlock and does not prove there are no subsequent native readiness gates.

## Escape ship

Preserved and tested Claude's uncommitted change: instantiate `REACTOR_GETAWAY_SHIP_OBJECT`, unlock and power on its paired device, then open it without snapping. A regression verifies this order after accepted cell delivery. The authored ship model is 80BFDDC2, with components 80BFDDC0, 815B8D64 and 80C70CAE. Actual flight/animation still needs visual confirmation.

## Remaining surge screen effect

Beam pose is user-confirmed and its corrected polarity is preserved. No additional screen-effect trigger has been established or installed. The alarm sequence resource 80BD1525 is a model with components 80C70B84 and 80F1F165; those are extracted in `build/first-encounter-audit/tags`. Inspect their effect graph before inventing another hop-on or treating a label search as proof that the effect does not exist. The existing alarm sequences remain triggered during the warning phase.

## Validation and install

Release DLL build passed, all 22 portable tests passed, all five Lua suites passed, and `git diff --check` passed. Full-route simulator peak: 234 variables, 61 intents/event, three timers, 13,000 instructions/event. These tests do not validate rendered effects.

Installed 19 files, including the settings change, with complete prior-file backups and SHA-256 verification. No game launch, save edits or SDK changes.

Backup: `/home/millie/Documents/Sunrise-builds/mission-ember/build/post-opus-install-backup-20260906-150504`.

Installed DLL SHA-256: `be4ea67c9399c4f08c41eac25f9ed2f27152dff9876b449e8d0d60e7fc9a0139`.

## Scope correction

Loading suppression is now limited to 1AU using the native loading activity reader above. The corrected Release DLL built successfully, and offline signature/call validation passed. Installation was deferred because the game was running; the installed DLL still has the earlier global override until this build is copied after the game closes. The scope-only installer is `/tmp/install-ember-loading-scope.py` and preserves settings, saves and scripts.
