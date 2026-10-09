# CitixChase v1.0 — Chaser balance and objective commitments

## Final gameplay

| System | Behavior |
|---|---|
| Runner reveal | Continuous during Pursuit. Projected cyan square brackets and distance through walls; off-screen direction pointer and existing ground navigation ring. |
| Chaser engine | 2.7 times base force up to 80 km/h, smoothly tapering to 1.82 times by 160 km/h. Maximum speed remains 292.5 km/h. The high-speed force retains the earlier 40% buff. |
| Ice Wave | 56 m range, five-second freeze, 50-second refill. Two-charge capacity, starting with one. Existing moving local scan and stationary world frost remain. |
| Relays | Six randomized, safe connected-road locations, at least 180 m apart and outside the central cluster. Collect by remaining within 8 m and below 60 km/h continuously for one second. Leaving or reaching 60 km/h resets progress. Supports the existing vehicle/on-foot collection flow. |
| Escape | After six relays, remain inside an exit in a functioning occupied car for four seconds. Leaving the exit or losing the car cancels progress. Both players see the countdown and progress bar. |
| Objective markers | Thin 8 m boundary rings. Relays are blue; exits green. The exit's former solid beam is removed so the waiting runner can see the car and street. |
| RMB Rapid Brake | Driving chaser only. One charge, 20-second refill, strong gradual braking over a bounded 0.8-second activation. Throttle/boost are blocked during activation; vertical physics remains intact. Blue/white wheel streak sparks are shared with peers. |
| Brake meter | Uses the runner gate-meter location. Ready, active and recharge states; gate-drag feedback remains. |

Normal maximum speeds, ram damage/separation rules, smoke, replacement-car rules and round time limits remain. No new account login or EOS setup is required.

## Scenery distance

| Preset | Fade starts | Cutoff |
|---|---:|---:|
| Low | 220 m | 300 m |
| Medium | 350 m | 500 m |
| High | 600 m | 850 m |
| Max | 1,000 m | 1,400 m |

These world distances cover bushes, tree foliage/trunks, metal/dark props, poles and lamps, compensating for Unreal's global multiplier. Gameplay gates have no distance cutoff. Density and tiny-screen-size rejection no longer bypass the 200 m minimum. Frustum and occlusion rejection remain. Low still uses cheaper rendering settings; longer visibility costs rendering work and has not been benchmarked across low-end hardware.

## Implementation and proof

The existing authoritative interaction map and replicated player-state progress are reused for relay and escape commitments. Automatic commitments use server-clock deadlines rather than subtracting a full first-frame delta. Capture and existing abilities retain their own behavior. Public relay entry cannot bypass the one-second requirement. Progress resets rather than pausing when eligibility is lost. The completion list fits the two-player limit in inline storage.

- Fresh full Citix automation: **27 passed, zero failures, exit 0** (`Saved/Logs/Commitment-VerifiedSuite.log`). Covers exact one/four-second deadlines, the 8 m and strict below-60 thresholds, acceleration taper/top-speed ceiling, removal of the occluding exit beam, boundary geometry and hiding, plus existing multiplayer/graphics/vehicle/Ice/host-travel checks.
- A two-process game test exercised actual authoritative mode updates and replicated progress: rejection at 70 km/h; partial relay sync; reset after leaving; reset after speeding up; collection of all six timed relays; partial escape, cancellation, re-entry, and success only after four seconds. Host and joining-client receipts confirm replicated relay and escape progress. Fixtures inject car poses/velocities and do not represent a natural driving or fairness playtest.
- Earlier two-process input proof verified real remote RMB/RPC, gradual braking, 20-second refill and uninterrupted reveal for 35 seconds. The actual generated city yielded 900 safe candidates and six separated relays for diagnostic seeds 11 and 19.
- Evidence is stored under `Docs/evidence/chaser-balance` and `Docs/evidence/objective-commitment`. Displayed version stays v1.0; multiplayer protocol **3** rejects earlier replication layouts. Both peers must copy the final updated folder.
- No new physical two-device EOS match or hardware-wide performance benchmark is claimed.

## Self-review

Important qualities: readable interception opportunities, responsive recovery from corners, clear shared escape feedback, and restrained low-poly presentation.

The first review found a large opaque exit beam blocking the runner's view for the entire wait, an unclear physical relay boundary, and the risk of a frame-dependent early completion. Refinements replace the beam with a green projected boundary/core, show the true 8 m relay boundary, and use exact server-clock deadlines. The existing HUD card style provides progress and cancellation feedback without a new widget system. The final visual check reviewed both roles and the runner's surrounding street, not only the countdown card. Probe teleports are aligned to the road, and screenshots wait for replicated objective state and chase-camera settling; production camera behavior is unchanged.

Scores after refinement: interception-rule clarity 8/10; handling structure 8/10; shared countdown feedback 8/10; visual coherence 8/10. Actual competitive fairness needs friend matches. Further decoration would add clutter. The remaining deliberate limitation is that these projected rings approximate a planar road surface across curbs.

## Remaining optional experiments

The relay commitment, escape countdown and low-speed engine curve requested after the initial review are implemented, not proposals.

If contacts still feel unfair, first distinguish a real hit from parallel rubbing: current rams require 15 km/h closing speed, separation and a 1.5-second interval. Test a 10–12 km/h threshold separately if glancing hits are being rejected too often. A small capped velocity-lead marker could also help interception without steering assistance. Neither further experiment has been added. Compare untouched escapes, accepted rams and round durations over a few friend matches before adding another buff.

## Final release

Shipping BuildCookRun completed successfully with exit 0 (`Saved/Logs/Commitment-FinalPackage-v1.0.log`). The final v1.0 package replaced `D:/_YienStudio/CitixChase`; all 34 deployed files were verified against SHA256 checksums (`Saved/Logs/Commitment-Deployment.txt`). The previous installed release was backed up before replacement. Final multiplayer receipts and both-role escape screenshots are in `Docs/evidence/objective-commitment`.

Installed Shipping smoke checks exercised the actual lobby Host LAN and Host Online controls. Both produced a connected listen server with verified city; online used CitixEOSNetDriver. Receipts are in the objective-commitment evidence folder. Owned test processes were closed after verification.
