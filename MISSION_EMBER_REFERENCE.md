# 1AU reference and implementation evidence

Reference: https://www.youtube.com/watch?v=PqurUhqC2CE (36:42).
A local low-resolution video copy was retrieved on 2026-09-05 for timestamped frame inspection.
The recording starts with player control at Starboard Landing; it does not show the arrival
cinematic. Its absence here is not evidence that the original mission lacks one.

| Video time | Visible event | Script/package counterpart | Verification |
| --- | --- | --- | --- |
| 00:00 | Starboard Landing, initial objective | powerhouse state, region 64 | video and SDK |
| ~00:05–00:15 | Ghost arrival line and Ikora central-core guidance | dialog sensor 80B3C90A, cue 0 | video subtitles and SDK variants |
| ~00:25 onward | Landing enemies, including shielded Cabal and melee enemies | landing squad family | individual squad-to-actor mapping still under audit |
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
