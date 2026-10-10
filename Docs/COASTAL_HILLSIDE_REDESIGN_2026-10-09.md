# Low-poly coastal hillside redesign

The current project was preserved first as GitHub commit `d708bdaa2ef6e6338dd2e5614dfe8f2c85819195`, titled **V2 new map change**, on `main` and tag `v2-new-map-change`. The redesign is on `hillside-coastal-redesign`. The installed game at `D:\_YienStudio\CitixChase` was not replaced.

## Implemented result

One connected, irregular coastal landmass replaces the rectangular platforms. Five sampled mountain hairpins climb to approximately 90 metres, with a roughly 110-metre mountain silhouette. The compact default footprint is 700 by 640 metres. The coast, village circuit and eastern ridge form alternate chase routes; a tunnel shortcut and supported ravine bridge serve actual geographic crossings. Shared junctions have level approaches, retaining walls stop clear of turning areas, and dead ends have a full-car stopping apron. There are no circular intersection caps.

Warm-roof houses occupy three elevation bands, with varied intervals and setbacks. The village clock, summit communications tower and marina provide orientation. Rock cliffs, modest cypress vegetation, road markings, guardrails and lamps use shared low-poly assets and instanced groups. Landmark foundations reach terrain; houses reserve space around landmarks. Warm afternoon lighting, restrained fog and simple opaque ocean material preserve the existing art vocabulary.

The layout is built once and shared by the generator, visual builder, dry-land checks and gameplay. Hillside parameters are grouped in `CitixCitySettings`; the default seed is reproducible. Revision 4 and the geometry/settings hash protect host/guest agreement. The existing City map remains available. Host lobby map selection, two starts, fourteen relay candidates, six active/five required, two exits and four recovery sites are retained. The twelve requested adjustments remain documented in `ADJUSTMENTS_VERIFICATION_2026-10-08.md`.

## Actual screenshots

These are game captures, not concept images:

- [Overall structure](evidence/coastal-redesign/final/Hillside-Aerial.png)
- [Coastal composition](evidence/coastal-redesign/final/Hillside-Coast.png)
- [Mountain switchbacks](evidence/coastal-redesign/final/Hillside-Switchbacks.png)
- [Village](evidence/coastal-redesign/final/Hillside-Town.png)
- [Tunnel entrance](evidence/coastal-redesign/final/Hillside-TunnelPortal.png)
- [Summit](evidence/coastal-redesign/final/Hillside-Summit.png)
- [Marina](evidence/coastal-redesign/final/Hillside-Marina.png)

## Three review and improvement cycles

1. **Structure first:** replaced stacked platforms and narrow generated roads with the irregular landmass, broad contour routes and five deliberate climbing hairpins. Actual rendered road winding and collision exposed incorrect mesh winding and lane-plane distortion; both were corrected in the shared builder.
2. **Composition:** screenshots exposed tunnel-cut sky holes, regular village rows and a foreground ridge obscuring the mountain. The tunnel now clips only its actual bore and has sealed rock cut faces. Houses have varied placement across three neighborhoods; the foreground spur is lower and broader. Lighting participates in multiplayer identity. Review-stage screenshots are retained in `evidence/coastal-redesign/review-2`.
3. **Driving and grounding:** keyboard-controlled driving exposed walls extending into junctions and overlapping grade planes. Walls now stop clear of intersections, junction arms have level landings, and the ridge joins a straight section so village bends remain smooth. Landmark foundations and placement were corrected. A redundant summit spur was removed after the full-car clearance check found its proximity to a ridge guardrail. The scripted driver uses a six-metre waypoint arrival radius within the fourteen-metre road, without subsequent teleports or forced movement.

Final self-assessment: coastal mountain identity **8/10**, dominant climbing roads **8/10**, coherent village composition **7/10**, low-poly consistency **9/10**, driving readability **8/10**. Remaining weaknesses are repetitive simple house silhouettes, inexpensive flat ocean shading and large deliberately open hillsides. These do not warrant more assets or rendering effects for this scope.

## Verification

- `Saved/Logs/Coastal-Complete-Tests.log`: **30/30 tests pass**. Covers connected graph, maximum 12% grades, minimum 24-metre authored curve radii, actual road/terrain collision, traffic tyre contact, objective guidance, tunnel car-volume clearance and sealed shoulders, rotated spawns, full-car landmark endpoints, teardown, and existing gameplay regressions.
- Actual driving uses the existing movement component and simulated W/S/A/D input after one initial fixture pose. Traffic is removed only in these isolated handling fixtures. `ChaseMP_CoastalSedanDone_Host.log` and `ChaseMP_CoastalBusDone_Host.log`: all five hairpins completed in **150.1 seconds**, health **100**. `ChaseMP_CoastalVillageCurve_Host.log`: village circuit **63.2 seconds**, health **100**. `ChaseMP_CoastalTunnelFinal2_Host.log`: tunnel **41.1 seconds**, health **100**. Earlier failed runs remain in Saved/Logs rather than being presented as successes.
- `ChaseMP_CoastalGameplay_Host.log`: real two-peer hillside match with 100 ms packet lag/2% loss; five relays, second escape gate, escape without crash, role swap, recovery delay, replacement vehicle integrity and rematch startup. `ChaseMP_CoastalMapSelect_Host.log`: guest map change refused, host hillside selection verified by both peers, readiness reset, City restored.
- `Coastal-Complete-Shipping.log`: Shipping build/stage/archive succeeds. Separate package: `PackageHillsideV2/Windows/Citix.exe`. No installer/deployment step was run.

Final packaged multiplayer and matched performance receipts follow. The tests are automated local fixtures, not a claim of extended human testing across two physical computers. Driving with arbitrary live traffic and all non-default geography settings remains outside the measured fixture scope.

## Measured final performance

Matched 1600x900 rendered overview captures, seed 47821, quality level 2, 17:30 lighting, VSync off, fixed camera and traffic disabled. Compared the final 2,000 rendered frames of each 4,000-frame capture against the preserved checkpoint. Mean frame time: **4.463 ms -> 4.777 ms (+7.05%)**, within the <=10% target. Mean draw calls: **174.125 -> 174.488**; rendered primitives: **35,158 -> 49,203**. This is a controlled overview comparison, not a guarantee of every hardware configuration or a crowded live chase.

Raw CSVs, comparison JSON and the stdlib-only reproduction command are retained:

`python Tools/hillside_performance_report.py Docs/evidence/coastal-redesign/performance/Baseline.csv Docs/evidence/coastal-redesign/performance/Current.csv`

The final independent read-only review found no concrete blocking defect in the source and seven final captures; it independently counted all 30 passing tests. Its scores were geography 8, switchback clarity 8, village 7, atmosphere 8 and low-poly coherence 8. The temporary baseline checkout was removed after preserving the measurements.

The standalone package totals **763,720,262 bytes (728.34 MiB)**, including the runtime and cooked content. Because the redesign generates its terrain and roads in code and reuses shared meshes/materials, it does not add a large external map-asset library. This total is not a measurement of map-only disk overhead.

## Packaged multiplayer proof

`ChaseMP_CoastalCookedRendered_Host.log`: rendered Editor listen host plus an actual **Shipping packaged guest**, configured 100 ms lag/2% loss. Guest identity acknowledged with seed 47821 and matching map hash. Passed countdown, five relays, second exit escape without crash, role-swapped second round, first wreck, replacement rejection before 40 seconds, replacement entry after the delay, second-car destruction and rematch startup. This is one two-round match followed by rematch startup; it is not two Shipping hosts or a completed second match. A preliminary NullRHI packaged UI join was inconclusive; the actual rendered lobby join succeeded.

Extracted pass receipts are committed in `evidence/coastal-redesign`; full diagnostic logs remain under Saved/Logs. The Shipping executable SHA256 and package size are recorded in `packaged-proof.json`. No installed-game file was modified.
