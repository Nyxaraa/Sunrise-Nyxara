> Current correction: the native damage-object-only escape test did not burn the player. The new implementation uses one player attachment of the authored sunburn effect and keeps that object disabled. See [final corrections](mission-ember-final-corrections.md) for evidence, scope and current validation; earlier experiments below are historical.

# 1AU Apex and ending corrections — 6 September 2026

## Current implementation

The ending route now uses the native pre-rendered player directly; see [the current ending mechanism](mission-ember-prerendered-ending.md). The world-transfer experiments below are retained as historical failure evidence. Direct playback still needs live confirmation.

- Cooling doors open after the surge stops. Visual/mechanical intervals remain 14 seconds closed, 6 seconds surging, 10 seconds exposed.
- The user confirmed those visuals/mechanics align, but hears the surge at cooling-door opening. The next live test requested another four seconds of lead. Audio sequences now pre-roll at closed-window second 4, ten seconds before the visual surge. This is playtest-based compensation, not a recovered native delay parameter. Pending audio is phase/region/generation guarded and cancelled on reset, core destruction and deposit.
- Initial beam: snapping to the resting endpoint still showed the incomplete beam in the first user screenshot. Native device position/power both reached 1. The new candidate seeks the driven endpoint once on laser creation, then animates to the resting endpoint so authored animation events can run. Duplicate presence and late presence after deposit cannot restart it. The target is the second user screenshot's thin, continuous beam. Needs visual confirmation.
- Escape damage: after the user confirmed doubled damage persisted, the scripted rail-wide burn was removed. Deposit disables the climb hop-on and enables only the placed `SUNBURN_DAMAGE_OBJECT` (type 4, Apex slot 17, model 80B82486). Initialization and escape completion disable the object; checkpoint hazard reconciliation selects its state. No scripted damage multiplier or second escape attachment is applied. Native damage/volume behavior still needs live confirmation.
- Escape ship: wait for native object presence, unlock, power on, seek closed, animate open. Duplicate presence does not restart it. Flight remains subject to visual testing.

## Escape trigger evidence

The tested installed run resolved `AD062E98 / type 31 / slot 2`, volume 17, at t=451535. This is APEX_DIRECTIVE_REACTOR_RAILS_ESCAPE_PLAYER_TRIGGER. At t=451650 it selected the ending state; at t=452290 cinematic authority was staged. Therefore the escape trigger fired. The failure followed it.

## Ending: corrected native-world model

The contributor's IKORA-ANIMATION-AND-ENDING.md describes retirement, native travel, exact resource/owner readiness, offered authority, revision-qualified active playback, inactive completion and post-cinematic handoff. Its Omega-specific asset/event hashes are not Ember inputs.

Earlier Ember code incorrectly treated packed regions 0, 1 and 2 as one instantiated world because they share bubble 0. Direct native instruction inspection corrected this:

- 4C8E60 registers the base world at bubble*8.
- 4C8E40 registers alternates at bubble*8+(alternateIndex+1).
- 436530 rejects global per-bubble state bytes >=1 and separately registers alternate entries. The message-1 byte is not how to select an ending variant. Leave that encoding unchanged; the reverted experiment caused Lime.

The 16:20:23 stalled capture contains 479 allocated sync records and no type 6. Publishing a roster subset without actual native travel did not create the movie controller. This supersedes the same-world hypothesis and the insufficient 7ff004d roster-only fix.

Current candidate:

1. Exact escape trigger claims the ending once.
2. Select bookend region 1; native message-12 travel is armed even though its bubble matches Apex.
3. The arrival lease closes only on exact packed-region arrival. Ordinary traversal can satisfy an already-held exact destination, but base Apex 0 cannot satisfy bookend 1.
4. Lua waits for held_region_index=1 before offering type-6 playback. A pending/current-only report does not offer it. Existing host publication gates still apply.
5. Only the exact controller's started incident records playback. Preserve its runtime-object identity as a string.
6. Skip sends stop authority; it does not complete the movie. Only the matching native terminated incident after start advances to bookend 2, which goes through its own travel/arrival/offer.
7. Complete native lifetime only after the second movie finishes. Ignore stale, unstarted, wrong-controller and duplicate receipts. Gameplay callbacks stop publishing encounter actions once the ending owns the route.

The subsequent test froze at t=465615, just after the t=465535 escape state selection and t=465590 native teleport. The sense probe saw a cycling freed-record chain; the native frame then reported a `network_send` job stalled. The 16:49:39 capture contains zero registry groups. Evidence is retained in `build/first-encounter-audit/ending-freeze-20260906-1651.log` and the corresponding capture folder. This localizes the failure to teardown; it does not prove every cause of that stall.

New candidate after this failure:

- Retired per-bubble keys remain at their wire ordinals with clear presence bits; phase two omits their object bodies. Active keys and state-byte order remain intact. A bounded Apex key history preserves those positions across both bookends, appending each new movie after existing keys. A cleanup timeout cannot re-enable already-retired groups.
- For 1AU bookends, the host identifies old local keys from the roster and retains scenario-wide services. Before sending removal, a read-only frame observer must see old keys in the native registry. After removal, the same registry owner must still exist in the same source region, with a nonempty registry and none of those keys remaining. Unknown/empty worlds, different owners, other regions and partial removal cannot release travel.
- The selection intent waits for `ev=mission_retirement result=native_cleanup_complete` before arming travel. This observes native descriptor removal; it does not manually free records, skip readiness checks, or call cleanup from the network thread. The request is bound to session, ActivityClient generation and lease revision. Existing intent expiry bounds a stalled request.
- Ending-only host teleport uses the prior local command token plus one (skipping zero), echoes the independent native world-transition token, and waits for local state 3 with matching command token, destination and hash plus the actual current region. A matching local state 0 retires the command. Intro travel retains its existing policy.

Remaining differences from the full Omega contract: exact cinematic resource/owner readiness and controller-revision-qualified completion are still not implemented. Current movie guards use typed native incidents and exact arrival. The teleport hash still uses the existing scenario lookup; its bookend spawn-set semantics need checking if arrival fails. Do not claim full ending playback is fixed until a live test reaches both movies. The new checks specifically address the observed teardown boundary.

## Live retirement refusal and correction (17:21 run)

Installed commit 48815f9 reached the escape trigger at t=273816, queued retirement at t=273924, and refused the intent at t=275663 with `native roster cleanup did not complete`. No native travel or ending playback started. The simulation continued, matching the reported black loading screen with world audio. Captures are in `build/first-encounter-audit/bookend-live-20260906-172136.log` and `reactor-runtime-20260906-172514`.

A read-only capture of the retirement request showed failed status 4, source region 0, and zero requested keys. The native registry still contained nine groups: one scenario-wide service and eight Apex-local groups. The cleanup list was assembled during initial mission seeding, before runtime squads, authored authority and Scene groups were appended. This was an empty request, not proof of a native cleanup timeout.

The correction finalizes retirement after the entire roster is assembled. It also keeps the current world's loading/spawn gate open while bookend retirement is pending and native travel has not been armed. Selecting a destination alone must not black out the screen if cleanup is refused. A new request log records source region, key count, status and lease revision. The next test should demonstrate a nonempty request (eight local keys for this captured roster), baseline observation, native removal, qualified travel, and then movie startup in that order. This run's refused Lua ending request does not retry automatically; testing the correction requires a fresh run.

The corrected build and the same 23 portable tests and five Lua suites pass. Native retirement and ending playback still require live confirmation.

## Package evidence correction

Config 80B3D494 (ship) and 80B3D497 (sunburn) share fallback resource 80BFDDC2 at offset 0x538. Their actual placed entries at 0x580 are 80B71228 and 80B82486. Ship model 80B71228 includes device component 80C70C0E and animation component 80B71226; placement 42BF7017F2901B45 matches its device. Component references have 12-byte stride. Do not infer ship capabilities from the fallback model.

## Validation and outstanding work

Release build, all 23 portable tests and all five mission Lua suites pass. Route peak: 238 variables, 61 intents per event, four timers, 13,000 Lua instructions including mock. Coverage includes exact bookend arrival, stale native incidents, skip/finish ordering, audio pre-roll, cooling order, startup deduplication single-source escape scorch, native retirement qualification, preserved wire ordinals, absent retired authority bodies, and exact teleport tuple matching.

The full-screen surge effect is still unresolved. The new guide points to authored Scene inputs/source bindings; no arbitrary Omega event hash or substitute damage effect has been added.

## Follow-up scorch correction

The user still observes doubled scorch with the rail burn enabled and the sunburn object disabled. Deposit now explicitly disables the pipe hop-on, waits 250 ms, then attaches the rail burn. Hazard mode is idempotent, so repeated callbacks do not issue additional attachment revisions. This addresses a possible overlapping attachment during an enabled-to-enabled filter change; the native cause and damage rate still require visual confirmation. The sunburn object remains disabled.

## Installed a8d3064 live result (17:37 run)

The intro started and skipped successfully. At t=251282 the exact escape trigger fired. Retirement requested eight keys from source region 0 at t=251748, observed the baseline at t=251756, and completed native cleanup at t=253805. Native travel was armed at t=253815 and started/synchronized to packed region 1 with command token 2 at t=253897–253898. This proves the final-roster correction fixed the empty cleanup request.

At t=253908 the native sense probe reported a record-chain cycle; subsequent native asserts reported a stalled `network_send` job. The read-only capture `build/first-encounter-audit/reactor-runtime-20260906-174128` contains zero registry groups and no movie controller. No ending cinematic-start incident arrived. Successful old-group removal therefore does not by itself resolve the subsequent world teardown/record-chain failure. Preserve this distinction: playback readiness has not yet been reached. The fixed snapshot is `build/first-encounter-audit/ending-freeze-a8d3064-20260906-1741.log`; the continuous capture is `bookend-live-20260906-173744.log`. Scorch damage/appearance cannot be verified from these logs.

## Native-only escape hazard

The detach/delayed-reattach experiment did not resolve the reported doubled damage. It is superseded: escape now enables SUNBURN_DAMAGE_OBJECT and leaves REACTOR_COFFIN_INTERIOR_THERMAL_HOP_ON disabled. The latter is retained only for the climb pipes before deposit. The SDK exposes the sunburn as a placed type-4 object, not a standalone type-60 trigger volume; its precise damage implementation has not been recovered. Tests assert activation at deposit, no enabled scripted damage attachment during escape, duplicate callback idempotence, and deactivation at the escape trigger. The ending transition freeze remains unresolved.
