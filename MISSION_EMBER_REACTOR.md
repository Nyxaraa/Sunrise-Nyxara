> Follow-up: the latest live run reached the escape endpoint but reported missing beam effects/hazards, a stale mission banner, inaccessible Controllers, and a freeze before the ending movie. See the newest entry in MISSION_EMBER_STATUS.md for the fixes and their remaining gameplay checks. The earlier candidate below was not visually validated for those behaviors.

# 1AU reactor sequence — 2026-09-06

Reference: [1AU playthrough, 21:20–28:30](https://www.youtube.com/watch?v=PqurUhqC2CE&t=1280).
Reviewed the cached video across that interval at five-second spacing, then at one-second
spacing around 23:28–24:27, 25:32–26:39 and 27:15–28:04. Subtitles informed dialogue placement;
this was visual inspection, not an audible validation of the music mix. Contact sheets live
in `build/reactor-audit/`.

## Reference sequence

| Time | Observed behavior | Implementation |
| --- | --- | --- |
| 21:25–22:55 | Interceptor/security fight, then access to Light's End | Two authored single-Psion squads `DISPENSER_SUPPORT_A/B` form the Electron Controller gate; boarding and support deaths do not open the security door. |
| 23:00–23:10 | Ghost reacts to the weapon, then identifies thermal exchangers | Cue 40 at entry, cue 41 nine seconds later; duplicate triggers do not interrupt/replay either. |
| 23:38–23:44 | Beam/screen surge and transition, then opening clamshell shutters | Native placed laser/ring objects are active. Their device transitions and alarm sequences precede the exposure window. |
| 23:44–23:54 | Purple target exposed for roughly ten seconds | Six-second warning, ten-second exposure, fourteen-second recovery. This thirty-second clock is reconstructed pacing, not a recovered original script. |
| 23:51 | First target destroyed; “That's one!” | Valid health/death receipt sets one durable completion bit and activates its authored destruction scene. |
| ~24:55–25:00 | Second target destroyed; access across bridges to central island | Both deaths extend both bridges, unlock/power central doors, instantiate the central target and activate coffin squads. |
| 26:00–26:04, 26:25–26:30 | Central target visible, shutters close, reopen, then target destroyed | Central doors and shield inherit the same clock, including an exposure already in progress. They continue cycling until central target death. |
| 26:30 | Ghost says temperature increase is insufficient | Cue 48; leave central housing open, unlock the fusion-cell route. |
| 27:28–27:38 | Ghost identifies fusion cell; player picks it up | Cue 50 via approach or first ownership receipt. Native `MOTHER_BRAIN_CARRY_OBJECT`; recovery handles expiration. |
| 27:45–28:00 | Cell carried to reservoir and inserted | Carry navpoint switches to `EMBER_DIRECTIVE_REACTOR_MOTHER_BRAIN_DELIVERY_NAV_POINT`; accepted deposit consumes the cell and begins escape once. |
| 28:01–28:30 | Overload/explosions, escape objective, Ghost calls Zavala | Existing authored explosion scenes/escape devices, cues 51–54, escape checkpoint and two ending cinematics retained. |

## Mechanisms fixed

`80809562` damage Sense fields health and shield have reflected flags `0x21`, meaning
mandatory raw float fields. Optional presence is bit `0x02`. The previous decoder consumed
nonexistent presence bits, misaligning both health and the Auth revision. Native `A774B0`
publishes the fixed twelve-byte health/shield/revision state. Independent complete packet
fixtures now cover healthy, zero-health and absent-target (-1) states, plus truncation.
Lua accepts only the current encounter generation and a previously observed living target.
Unloading an object, initial zero, old generations and duplicate deaths cannot advance it.
Redundant lethal-effect requests after an already acknowledged target death were removed.

Sequences previously searched only the opening seed state. Scene lease validation likewise
rejected a later local state even with a current connection/region. The resolver now prefers
the exact live-region occurrence; the existing seed occurrence remains a fallback for shared
mission objects. It retains scenario, object, publication, connection generation and ambiguity
checks. Scene validation also accepts the exact live state. This addresses the recorded
`target_unavailable` alarms and the later-area scene `wrong_state` failures without changing
the selected mission state or republishing the opening area.

Device position, power and lock are independent lanes. The central doors are initially
closed/locked, then explicitly unlocked/powered at the second clamshell death. The reactor
bridges use position 1 initially and position 0 to extend, consistent with the reported inverse
pose and the landing bridge. This final-area polarity still needs visual confirmation.
Beam VFX use the authored `SPECOPS_APEX_RING_LASER_DEVICE`/`RING_DEVICE` and their placed
models (`80B7117C`, `80B711C6`, `80B71218`); no fabricated overlay or model replacement.
The exact beam/screen response and animation latency require an in-game check.

Native music sections now advance through access, controller defeat, weapon reveal,
thermal-exchanger combat, first destruction, central access/exposure, cell and escape
(19–28). Section identities beyond the recovered `tumbler_exit` are inferred from bank order;
finale section names and audible transitions have not been independently verified.

## Validation and installation

- Release DLL builds; 21 portable native tests pass, including mandatory damage packets and
  live-region behavior selection (missing area, cinematic state, foreign scenario, ambiguity).
- Five Lua suites pass. Full route exercises both-controller gating, warning before exposure,
  both-clamshell bridge gate, central object creation and repeating doors, stale/missing health,
  wipe generation, cell expiration/recovery, consumption, duplicate events and ending cinematics.
- Peak full-route usage: 228/512 variables, 61/63 recorded intents per callback, 2/32 timers,
  13,000/100,000 instructions per mocked callback. Reset publishes cleanup before repopulating.
- Core initialization is deferred while outside region zero and idempotent on duplicate timers.
- Installation manifest: `build/reactor-finale-installation.json`. Existing save/settings/SDK
  and prior replication cleanup remain intact. Game launch is manual.

This is an implemented and regression-tested candidate. Native sound, screen surge, actual
bridge pose and shutter timing remain live-test observations, not established results.
