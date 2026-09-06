# 1AU Apex and ending corrections — 6 September 2026

## Current implementation

- Cooling doors open after the surge stops. Visual/mechanical intervals remain 14 seconds closed, 6 seconds surging, 10 seconds exposed.
- The user confirmed those visuals/mechanics align, but hears the surge at cooling-door opening. The next live test requested another four seconds of lead. Audio sequences now pre-roll at closed-window second 4, ten seconds before the visual surge. This is playtest-based compensation, not a recovered native delay parameter. Pending audio is phase/region/generation guarded and cancelled on reset, core destruction and deposit.
- Initial beam: snapping to the resting endpoint still showed the incomplete beam in the first user screenshot. Native device position/power both reached 1. The new candidate seeks the driven endpoint once on laser creation, then animates to the resting endpoint so authored animation events can run. Duplicate presence and late presence after deposit cannot restart it. The target is the second user screenshot's thin, continuous beam. Needs visual confirmation.
- Escape damage: deposit created SUNBURN_DAMAGE_OBJECT and also attached an extra rail-wide thermal effect. Reaching the end detached only that extra effect, matching the user's report that damage then became normal. The new script retires the climb attachment on deposit and leaves the native sunburn object as the escape damage source. No global damage multiplier changes.
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

Remaining differences from the full Omega contract: no explicit native old-roster retirement receipt, resource/owner readiness bridge, or controller-revision-qualified completion bridge has been implemented yet. Current guards use typed native incidents and exact travel arrival. Do not claim the complete guide has been ported or live playback has succeeded. Native movie creation/playback and teardown are the next live checkpoints. Compare existing message-12 hash/token handling with the contributor's spawn-set/token description if arrival stalls.

## Package evidence correction

Config 80B3D494 (ship) and 80B3D497 (sunburn) share fallback resource 80BFDDC2 at offset 0x538. Their actual placed entries at 0x580 are 80B71228 and 80B82486. Ship model 80B71228 includes device component 80C70C0E and animation component 80B71226; placement 42BF7017F2901B45 matches its device. Component references have 12-byte stride. Do not infer ship capabilities from the fallback model.

## Validation and outstanding work

Release build, all 22 portable tests and all five mission Lua suites pass. Route peak: 238 variables, 61 intents per event, four timers, 13,000 Lua instructions including mock. Coverage includes exact bookend arrival, stale native incidents, skip/finish ordering, audio pre-roll, cooling order, startup deduplication and removal of stacked escape scorch.

The full-screen surge effect is still unresolved. The new guide points to authored Scene inputs/source bindings; no arbitrary Omega event hash or substitute damage effect has been added.
