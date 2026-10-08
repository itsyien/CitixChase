# Gate expansion and immediate relays review

4 October 2026. Remake only; preserve E:/Citix/Citix.

## Implemented experience

Twenty road-wide gates replace the two one-use stations. Width follows the selected road corridor, with a minimum 12 m opening, solid edge posts, and an overhead frame. Placement uses separated, validated dry road midpoints and excludes bridges so gate structures do not obstruct bridge corridors. The complete footprint is checked against river geometry. Layout persists across role swaps.

Runner crossing provides five seconds of free boost. One global replicated recharge deadline is set to 15 seconds; other gates cannot restart or extend the boost. Every gate becomes grey to the runner during recharge, and a compact meter above the speed card shows remaining time. Each driver must leave and enter a gate again to trigger it; waiting inside does not farm activation.

For the chaser, gates retain red crosses and STOP pointers even during runner recharge. A crossing ramps down to 70% speed over 0.75 seconds, holds, and smoothly releases during the final 0.35 seconds of a three-second penalty. Crossing another gate while slowed extends the penalty without restarting the acceleration ramp. It does not globally penalize the chaser merely because the runner used a gate.

All five relays must be reached. Activation is immediate on server entry, driving or walking; duplicate entry does not count twice. A rotating blue holographic frame, floating signal core and orbiting pixels replace the old opaque pillar. Existing road guidance and 13.5 m eligibility radius remain.

Chase traffic population target and local cap increase by 50%; current configured target is 225 and local cap is 39. Existing simulation, retirement, takeover and client interpolation remain. An occupied chaser-car contact with a visible, untaken traffic bot starts a two-second stun. Repeated contact does not refresh the deadline. Movement input cannot overpower it; voluntary exit is blocked until the stun ends. Runner cars and empty parked chaser cars do not get this stun.

## Three review passes

1. Reuse the existing authoritative round, movement, beacon and particle systems. Initial regression checks fail for the old three-relay requirement, narrow gate and nonblocking edges. Native checks now cover five-relay unlocking, physical edge collision, width and smooth slowdown. A real Chaos car-to-bot collision triggers and releases the stun, rather than directly calling the stun method from the test.
2. Review host/client renders: ready yellow gates, grey runner recharge, red chaser STOP crosses and separate yellow/red effects are visible. Remove one-use state and dwell prompts. Reset role-specific slowdown/stun state at round start. Retain entrance latches, so cooldown expiry does not trigger a parked car. Avoid per-frame symbol rebuilds: only update crosses/arrows when the viewer's role changes. Extend dry-footprint validation to gate width. Include on-foot crossing effects and exclude parked-car traffic stun.
3. Fix the largest visible weaknesses: replace an empty-looking holographic cage with a floating signal core; recolour and reduce gate-boost exhaust so orange fire does not compete with yellow energy streaks. Reserve extra HUD space so navigation labels do not overlap the recharge/status strip; separate colliding relay/gate labels with a fine leader line. Keep only the edge posts physical so overhead beams cannot intercept dry-ground traces. Add a runtime surface check beneath all 20 gates. Preserve an existing slowdown ramp when another gate is crossed. No inventory, abilities or unrelated UI redesign added.

| Intended quality | Before | After |
|---|---:|---:|
| Clear role-specific gate meaning | 4 | 8 |
| Reusable runner escape opportunities | 3 | 8 |
| Immediate objective feedback | 5 | 8 |
| Visual coherence | 5 | 8 |
| Pursuit balance (provisional) | 6 | 6 |

Balance is unproven. Wider unavoidable gates, 20 hazards and denser traffic change chaser difficulty substantially; comparable-player playtests are required before raising that score. Automated success does not establish fair role win rates or subjective effect intensity.

## Evidence

- `Saved/Logs/gate-red-rules.log`: old behavior fails five-relay, gate-width and edge-collision checks.
- `Saved/Logs/gate-final-rules.log`: native rule checks pass.
- `Saved/Logs/ChaseMP_gate-final-native_Host.log`: 20 gates, independent runner boost/chaser drag, global cooldown and reuse, real bot collision stun, expiration without repeated-contact refresh, five immediate relays, traffic target 225. Rendered with 80 ms lag, 20 ms variance and 3% packet loss.
- `Docs/evidence/gate-expansion/final-native-latency-receipt.json`: completed feature probe.
- Final delivered archive, second seed, complete round loop and packaged evidence are recorded below after execution.


## Final delivery checks

- `gate-final-clearance-build.log`: Editor build succeeds. `gate-final-clearance-rules.log`: RoundRules succeeds. The new overhead-frame regression first failed in `gate-clearance-red-rules.log`, then passed with physical edge-only collision.
- `ChaseMP_gate-seed1234_Host.log`: the complete feature probe passes on city seed 1234.
- `ChaseMP_gate-full-seed5678_Host.log`: seed 5678 passes five immediate relays, escape, role swap, first wreck and ragdoll, pre-40-second replacement rejection, second-wreck defeat and rematch. Receipt: `evidence/gate-expansion/seed5678-round-loop.json`.
- `gate-final-clearance-package.log`: Shipping build/stage/archive succeeds. Child executable SHA256: `4B8C834FCA4932D53B412FF21E8AF526F3409F5824A8EEC175CDD45C4B765DAB`.
- `gate-original-verification.json`: 142 baseline original source/configuration files checked, zero changed.

Rendering evidence is from local paired instances at 720p. Separate-PC networking, extended traffic performance and comparable-player balance remain human playtest limits.

- Final packaged Host/Join/Ready probe: `evidence/gate-expansion/packaged-final-receipt.json`, freshly generated after the final rebuild, passes 20 gates, global cooldown/reuse, actual bot-collision stun, five instantaneous relays and traffic target 225. The probe also verifies unchanged dry-ground height beneath every gate before running those checks.
- Final packaged screenshots: `runner-ready-final.png`, `runner-energy-final.png`, `chaser-stop-final.png`, `chaser-drag-final.png`, `hologram-final.png` in the evidence directory. Inspected at 1280×720; role colours, cooldown, energetic yellow exhaust/pixels, restrained red drag and the floating hologram core are visible. Guidance labels are separated when targets overlap.
