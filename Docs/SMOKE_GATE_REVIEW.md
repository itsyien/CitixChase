# Sparse gates, runner handling and smoke

## User intent and final behavior

1. Reduce visual gate clutter: no four-gate crossroads, about one per 200 m and roughly 100 overall. Use a connected-road conflict graph with 200 m minimum spacing and at most 100 picks. Least-conflicting placements fit 86 in the default city, up from the initial sparse-selection attempt of 66. All remain road-wide with physical edge posts; existing boost/cooldown/role effects remain.
2. Remove traffic stun. Delete the authoritative effect, replicated deadline, forced braking/velocity stop, exit restriction, contact latch and HUD. Keep physical collisions, traffic density and interpolation.
3. Improve high-speed runner steering. Increase authority .78 to .98, retained high-speed lock .55 to .82, max wheel angle 34 to 36 degrees, response rate 24 to 32, return rate 36, friction 1.65 and lateral stiffness 1.35 times baseline. Apply identical role tuning on server and predicting client; keep 190/225 caps and chaser tuning.
4. Runner smoke: one initial charge, maximum two, refill every 60 seconds without restarting an active timer. Server validates role, pursuit, charges, recovery and ongoing release. Use LMB on foot or in the car; place at the activation spot and emit over five seconds. A 20 m grey billowing cloud lingers/fades by 15 seconds total. Use a separate animated, soft-edged translucent noise shader rather than the tyre-smoke spheres. Forty lightweight instanced billboard particles simulate locally from replicated position, start time and seed; no per-particle replication. Charges persist through possession changes and wrecks, reset each round. Remove live clouds on round initialization.

## Review loop

Pass 1: native regressions fail the old 150 m rule and lower runner steering authority. The first gate selection fits 64-66, so use finer safe placement choices and a least-conflict fill to approach the requested count without breaking spacing.

Pass 2: an initial smoke shader has an extra unconnected Custom input and falls back to the default material. Reset its inputs before authoring; rebuilding a loaded rooted material also exposed an editor assertion, corrected by replacing its expression collection without marking rooted expressions as garbage. The final authored material compiles and renders. Use a dedicated soft noise shader with depth fade, camera-facing billows and deterministic gradual growth.

Pass 3: review native early/full/fade images and the runner skill HUD. Move the card above recovery/integrity to avoid touching panels. A headless authority plus one rendered client avoids overloading the GPU while older user game windows are open. The full cloud distinctly blankets the road; release and refill stay compact on the HUD. No unrelated UI redesign or ability system framework.

| Quality | Before | After |
|---|---:|---:|
| Gate spacing and junction clarity | 3 | 8 |
| Runner responsiveness | 6 | 8, provisional feel |
| Smoke readability and concealment | absent | 8 |
| Resource feedback and clarity | absent | 8 |
| Performance confidence | 6 | 7 |

Observed rendered-client diagnostic frames during the native smoke preview: roughly 10.5-18 ms at 720p (samples, not a controlled benchmark). Long sessions, separate-PC latency, high-speed handling feel and role balance still require human playtesting. Do not equate automated success with proven balance.

## Verification

- `smoke-red-rules.log`: fails old gate spacing and steering authority. `smoke-final-rules.log`: succeeds, including four-arm junction selection, 200 m pair spacing, role handling and smoke refill/cap/no-restart boundaries.
- `ChaseMP_gate-smoke-final_Host.log`: 86 dry gates, 1,329 road checkpoints, worst nearest-gate road distance 192 m, boost/drag/cooldown/reuse preserved, physical traffic contact no longer starts a scripted stun, five relays remain instantaneous.
- `ChaseMP_smoke-visual_Host.log`: real client LMB RPC in car, gradual 11/40 early particles, repeated-use rejection, original deadline retained, real 60-second refill, old cloud expiry and LMB on foot. Native visual images in `evidence/smoke-gates` show early/full/fade stages.
- `smoke-package.log`: full Shipping build/cook/stage/archive succeeds, including the new smoke material.

## Delivery evidence and limits

- Latest runnable archive: `PackageSmoke/Windows/Citix.exe`; full cook includes the smoke shader. Child executable SHA256: `5D0785A91CDA67D1271CFFC296250B1554E5735260943C7C9AE9FB4D0021A72D`.
- Cooked joining client plus headless editor authority passed real car/on-foot LMB, gradual emission, real 60-second recharge and expiry; receipt `evidence/smoke-gates/cooked-client-receipt.json`. Both cooked full-cloud and on-foot screenshots were inspected: large distinct grey cover, readable role/resource HUD and clear centre outside smoke.
- Two-Shipping-instance attempt used the older normal archive because running user windows prevented mirroring. It produced no smoke receipt and is not a pass. `PackageShipping` remains the older build; use `PackageSmoke` on both sides.
- Fresh original hash verification: zero changes across 142 tracked original source/config files. Human handling feel, separate-PC latency, prolonged effects performance and balance remain unproven.
