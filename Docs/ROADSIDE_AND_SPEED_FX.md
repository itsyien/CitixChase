# Roadside interaction, progressive drifting and speed effects

Updated 2026-10-07. Version stays v1.0. Changes apply to E:/UnrealProjects/CitixChase only.

## Player experience

- Yien Studio billboard boards, posts and feet physically block cars. The space between the posts remains open; collision follows the existing structure.
- Roadside trees and decorative planters become low-poly bushes. Park trees remain. Each bush is a broad cluster of nine irregular faceted lobes with three small angular stems: 204 triangles, flat shading and varied green vertex colors. This follows the revised request for overlapping low-poly spherical forms rather than one sphere or fine leaf detail.
- Bushes are pass-through foliage. Chassis overlap applies exponential planar drag, with no solid impact and no modification of vertical velocity. The driver can steer and accelerate through them. Drag ends when overlap ends. A dedicated query channel avoids slowing cars for other city props.
- Holding A/D while drifting now builds steering and yaw assist progressively. Entry uses an exponential response at rate 8; normal steering and release retain their prior response. Both chase roles are covered.
- Three pale airflow ribbons curve over and around occupied cars at 90% of their nominal role/car top speed. Intensity rises toward top speed and fades quickly below the threshold. Boost cannot increase intensity beyond its cap. Destroyed or unoccupied cars lose the effect. This adds no gameplay force or camera overlay.

## Implementation and cost

The bush is one authored static mesh, instanced in existing city chunks. Its query box is slightly inset from its visible bounds. There are no per-bush actors or ticks; the car runs one object overlap query while moving. Bushes cull between 100 and 180 metres and do not cast shadows. Airflow uses one instanced component and 36 inexpensive plane segments per car; pulses animate in the material. Instance intensity uploads occur only after a visible change. Dedicated servers skip the visual components. The material/mesh authoring commandlet fully loads existing packages before updating them, so it can regenerate assets without partial-load save errors.

## Verification

- Saved/Logs/Roadside-Red.log records the new tests failing before implementation.
- Saved/Logs/Roadside-FinalSuite.log: 21/21 automation tests passed, exit 0. Includes existing LAN, online flow, graphics, Ice Wave and chase rules.
- Real Chaos driving tests at 30/60/120 Hz verify progressive steering, held turn, release, normal steering and airborne behavior. At 0.1 seconds of held steering, chaser/runner wheel angles are 19.53/22.68 degrees; sustained input still produces a substantial turn.
- BushDrag verifies reduced speed while remaining passable at 30/60/120 Hz. CollisionAndBushArt additionally queries the actual production city-chunk bush instance, tests billboard support hits and open-space misses, and enforces the 300-triangle art budget.
- Saved/Logs/Roadside-Assets.log: authored mesh has 204 triangles / nine lobes; assets saved successfully.
- Saved/Screenshots/Roadside-Bushes.png, AirFlow-On.png and AirFlow-Off.png were rendered and visually inspected. The isolated presentation fixture injects velocity into a fixed car pose; it is a visual/threshold check, not a natural driving test. At 96% role top speed, intensity was 0.698; at 80%, it was 0.000.
- Saved/Logs/Roadside-Package-v1.0.log: Shipping BuildCookRun succeeded, exit 0. The cooker manifest includes SM_CitixBush and both new materials.
- Saved/Logs/Roadside-Deployment.txt: 34 deployed release files verified by count and SHA-256 against the source package. The installed Shipping executable at D:/_YienStudio/CitixChase/Windows/Citix/Binaries/Win64/Citix-Win64-Shipping.exe started and remained responsive. This is a startup smoke test, not a complete packaged multiplayer playthrough.

## Self-review

Main qualities: coherent low-poly bushes, pass-through slowdown and physical billboards, gradual drift entry, readable high-speed air effect, bounded cost.

First pass: style coherence 6/10, interactions 7/10, drift response 7/10, speed feedback 7/10, cost 7/10. Highest-impact issues were excessive fine leaf detail, a test fixture using the wrong nominal speed after applying a chase profile, and unnecessary intensity uploads every visual tick. The single most valuable refinement was replacing fine foliage with nine faceted lobes, following the user correction.

After refinement: style coherence 8/10, interactions 8/10, drift response 8/10, speed feedback 8/10, cost 8/10. The bush silhouette and airflow now match the simple vehicle/city language. Real geometry queries close the gap between metadata checks and actual collision. Remaining tuning is subjective: exact foliage drag and drift timing benefit from driving feedback. No new physical two-device session was run for this update; earlier EOS relay proof remains historical evidence. Very low frame rates and unusually thin foliage may miss a brief contact because bush detection uses a current-position overlap, not a swept collision. The chassis and bush volumes make ordinary contacts broader.

Source/config backup: Saved/Backups/roadside-speed-drift-20261007-180440.
Previous installed release backup: Saved/Backups/deployment-before-roadside-20261007-182726.
