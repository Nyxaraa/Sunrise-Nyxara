# 1AU reference and implementation evidence

Finale review and current implementation: [reactor evidence](MISSION_EMBER_REACTOR.md).
The detailed September 6 review supersedes the earlier coarse finale rows below.

Reference: https://www.youtube.com/watch?v=PqurUhqC2CE (36:42).
A local low-resolution video copy was retrieved on 2026-09-05 for timestamped frame inspection.
The recording starts with player control at Starboard Landing; it does not show the arrival
cinematic. Its absence here is not evidence that the original mission lacks one.

| Video time | Visible event | Script/package counterpart | Verification |
| --- | --- | --- | --- |
| 00:00 | Starboard Landing, initial objective | powerhouse state, region 64 | video and SDK |
| ~00:05–00:15 | Ghost arrival line and Ikora central-core guidance | dialog sensor 80B3C90A, cue 0 | video subtitles and SDK variants |
| ~00:25 onward | Landing enemies, including shielded Cabal and melee enemies | catwalk entry/mid and pipe squad families | individual actor timing remains under audit |
| ~01:35–01:45 | Weapon active / disconnect it from the sun exchange | cue 2 | video subtitles and SDK variants |
| ~03:12–03:15 | Player approaches console and deploys Ghost | console ghost-link slot, type 65 | frame inspection at 1-second intervals |
| ~03:16–03:22 | Bridge machinery moves; crossing becomes available | six bridge devices | direction and live Auth delivery still need confirmation |
| ~03:25–05:20 | Crossing/far-side combat | bridge crossing and landing-sun families | visible encounter, not a reason to spawn all later enemies at launch |
| ~05:30 | Mineral Processing transition | link state, region 56 | video and SDK |
| ~07:15–09:45 | Processing fight and machinery progression | processing families, fusion-cell objectives | coarse frame pass; precise triggers pending |
| ~10:10 | Zavala/Ikora radio-silence exchange | cues 19–20 | video subtitles and SDK variants |
| ~11:00 | Ghost identifies exterior route | cue 23 | video subtitles and SDK variants |
| ~11:25–13:30 | Sunside traversal and combat | cinder state, sunburn families | coarse frame pass |
| ~13:55–20:10 | Internal combat and machinery, ending with the Bruiser fight | cinder later encounters | coarse frame pass |
| ~20:35 | Energy-stream travel | chute/tube progression | coarse frame pass |
| ~21:00–22:40 | Dark corridors, then Interceptor and Light’s End | apex access/security encounters | coarse frame pass |
| ~23:00–26:00 | Reactor combat and vent attacks | reactor clamshell/coffin families | coarse frame pass |
| ~26:00–27:30 | Fusion-cell pickup, carrying and deposit | reactor carry/deposit objectives | coarse frame pass |
| ~27:48–28:42 | Overload, escape run, cinematic transition | reactor escape and bookend state | coarse frame pass |
| ~28:42–35:27 | City counterattack and Ghaul cinematics | two apex cinematic states are candidates; identity needs confirmation | video only for ordering |
| ~35:54–36:42 | City arrival and Chosen gameplay | next mission | excluded from Ember scripting scope |

Times are observations from this player's run, not timers to encode in Lua. Cues and waves must
follow the corresponding spatial, interaction or combat event. The SDK contains declarations,
identities and data; those alone do not establish every authored script condition.

Live log evidence from the previous build: bridge Auth requests were staged at service ticks
97082–97268, before mission world entry at 102262 and held region 64 at 104814. Ten squad
requests were staged at 104915–107153. Transport staging does not establish visible state or AI.

## Arrival cutscene reference

Additional reference supplied by Millie: https://www.youtube.com/watch?v=IUmUYHELJAY
(21 seconds, inspected locally at two-second intervals on 2026-09-05).
The clip shows the player's ship approaching the Almighty, a view along its exterior weapon
structure, and a fade to black. Millie specifies the sequence as ordinary fly-in, then this
cinematic, then player spawn. The clip itself ends before the player spawn.

The controller selects authored cinematic state 49 and its type-6 bookend slot, then selects
playable state 64 only on that slot's termination event. Early region-64 reports cannot start
landing combat or guidance before that handoff. No 21-second timer is used: completion/skip
comes from the cinematic event. The selected package asset and the absence of a premature
player spawn still need visual confirmation in the running game.

## Opening-route correction

The start of the video precedes the catwalk fight; it is not the Mercury console platform.
Native build-data lists the `default` spawn cluster at approximately (-400, -192, -1.9).
The generated SDK's arrival-dialogue volume surrounds that cluster, followed by catwalk
15/35/50/80/100-percent volumes and then pipe/Mercury volumes. The previously selected
`powerhouse_landing_mercury` set around (-444, 74, -36.5) skips that opening route.
This spatial evidence corrects the earlier hash choice; a manual playtest must still confirm
which registered point the client actually selects and its facing/grounding.

Frame inspection was repeated for the opening contact sheet. It shows catwalk combat before
the pipe view and weapon-active exchange around 01:35. The Lua policy now stages the early
squad families and cue 2 on authored route events, with no video timestamps used as timers.

## Bridge video recheck — 2026-09-05 14:43

Half-second frame sequences3:10–3:34.5 saved in
`build/first-encounter-audit/reference-arrival-01.jpg`, `reference-arrival-02.jpg`,
`reference-drop-01.jpg`, and `reference-drop-02.jpg`.
Ghost deployment is visible around3:15; machinery moves3:16–22; ships sweep into view3:24–29
while the player crosses, followed by troop combat. The camera does not follow every ship
or show a complete passenger manifest, so these frames cannot establish all four cargo pairings.

Native curve captures prove that the earlier path packet selected the start marker. Entry/exit
now select marker1. See `transport-curve-markers-20260905.json` for all eight endpoints and
native parameter arrays. Values4/5 are spline coordinates, not elapsed flight seconds.


## Full route review — 2026-09-06

Inspected the saved reference video again using closer frame sequences in
`build/full-mission-audit/processing-*.jpg`, `processing-cell-*.jpg`, `cinder-*.jpg`
and `apex-*.jpg`. Offsets printed on those contact sheets are relative to the start
shown after the plus sign, rather than absolute mission time.

| Reference time | Observed action | Script gate |
|---|---|---|
| 05:30–06:40 | Processing entry fight and blocked grinder | Authored entry squads, obstruction/control volumes |
| 06:44–07:02 | Hologram explanation and fusion-cell pickup | Control trigger, native pickup/ownership receipt |
| 07:08–07:14 | Cell deposited; control becomes active | Accepted receptacle interaction |
| 07:15–09:45 | Defend machinery, clear rock obstruction | Three authored defense groups, accounted squad deaths |
| 10:18–11:18 | First Sunside ready room and window/exit | Room squads and exit devices |
| 11:18–13:30 | Exterior bridge and shade route | Authored deck triggers and native squads |
| 13:32–15:08 | Second ready room fight | Two groups; room checkpoint |
| 15:20–16:44 | Red passage, machinery rooms and ascent | Chamber/meat-grinder/ascent trigger sequence |
| 17:08–20:08 | Foundry/Bruiser, then exit opens | Three Foundry groups, hatch and ramrod devices |
| 20:32–22:00 | Energy stream, security area and Interceptor | Chute-volume restrictions and authored vehicle object |
| 22:50–27:20 | Light's End, exchanger/core destruction, cell | Native damage monitors followed by cell delivery |
| 27:30–28:40 | Cell overload and upper-rail escape | Native receptacle, scenes, escape-end volume |
| 28:42 onward | Escape exterior movie, then character cinematics | Authored STM/CNN states and matching termination incidents |

The recording establishes action order, not exact native condition logic. Vent cadence
is an explicit reconstructed18-second-open/8-second-closed policy pending live tuning.
The recording does not prove every reinforcement trigger or optional squad timing.
Native slot positions and combat objectives supply placement/AI. Do not describe these
Lua files as recovered retail mission scripts.
