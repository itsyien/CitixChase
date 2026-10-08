# Ice Wave and Yien Studio advertisements

## Intended experience

Chaser's LMB sends a broad icy scan forward. The effect should communicate range, direction and a successful hit without hiding the road. The runner immediately loses 30% of current horizontal speed and cannot accelerate or boost for three seconds; steering and braking remain available. The chaser starts with one charge, stores two, and earns one every 90 seconds during pursuit.

The city includes five roadside Yien Studio advertisements: three building screens and two freestanding billboards. Copy comes from https://yienstudio.net: “Imagination. Made Interactive.”, Yien Royale 2, Lava Parkour, Undead Rush, and games/apps. These are native scene objects, with a shared dark panel, cyan trim and text hierarchy.

## Product review and refinement

| Quality | First review | Final review | Refinement |
|---|---:|---:|---|
| Clear forward icy scan | 7 | 8 | Reduced competing wisps, added organic frost cracks, clipped ground frost to the cone |
| Immediate, fair control effect | 8 | 9 | Authority checks, boost lock, repeat hits refresh duration without stacking slowdown |
| Readable feedback | 7 | 8 | Role-specific charge count, recharge countdown, hit feedback and runner freeze timer |
| Advertisement clarity and placement | 5 | 8 | Native distance-field text graph, brighter lettering, dark panels, facade clearance and static-only placement checks |
| Coherence and restrained cost | 7 | 8 | Instanced geometry, finite effect lifetime, stop scan updates after fading, static advertisements without ticks |

The largest weaknesses were frost leaking outside the scan, traffic influencing advertisement placement, and unreadable text in the packaged game. These received priority over additional features. The font fix preserves Unreal's native distance-field sampling and explicitly checks the emissive connection rather than relying on a custom alpha threshold.

Independent code review identified three issues: cone clipping, dynamic traffic placement, and ground sign count when facade placement falls short. All were corrected. The verified default city has five signs, including three facade screens. Other procedural seeds may have fewer suitable facade locations; placement prioritizes a valid surface over forcing a floating screen.

## Verification

- Three Unreal automation tests pass: replicated ability state, cone/recharge boundaries, and existing round rules. Log: `Saved/Logs/IceWave-DeliverySuite.log`.
- Packaged Host/Join probe confirms 100 → 70 km/h immediately, three-second engine/boost lock, subsequent recovery, recharge arithmetic and two-charge capacity. The remote runner reports replicated freeze and release.
- Full multiplayer regression passes five relays, escape, role swap, first wreck/replacement restrictions, second-car defeat and rematch. Log: `Saved/Logs/ChaseMP_ice_full_regression_Host.log`.
- Shipping build is delivered separately at `PackageIceWave/Windows/Citix.exe`.
- Final packaged screenshots were inspected for the forward scan, runner freeze feedback, readable building screen and readable freestanding billboard. Screenshots, packaged host/guest receipts and the successful Shipping build log are preserved in `Docs/evidence/ice-wave/`.

The recharge interval is verified with simulated server times through the production rule; a real-time 90-second soak was not performed. Multiplayer verification used two local Windows processes. A separate-machine network session and GPU frame-time profile were not performed.

## Maintenance

`CitixMaterialSetup -IceOnly` authors the three ice materials and sign text material. The ability and billboard classes retain hard asset references so shipping cooks include them. Test fixtures only run with `-CitixIceProbe -CitixIceScreenshot`; normal play does not teleport cars or change cameras.
