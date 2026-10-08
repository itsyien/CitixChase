# Re-entry, relay dwell and night driving review

4 October 2026. Preserve E:/Citix/Citix; changes only in the remake.

## Review passes

1. Reproduced joining-client failure using actual local key input after server-owned exit/re-entry: W produced throttle 0 and speed 0.2 km/h. The outgoing character mapping was registered at higher priority than driving and only removed by authority UnPossessed. Cache the mapping subsystem, remove the context during destruction and controller replication, and reinstall driving on client restart. Replicate occupancy notifications so parking brakes and latched boost/handbrake inputs reset on both sides.
2. Change the existing server hold loop to begin relay dwell automatically for a runner in a car or on foot. Keep F available for car entry/exit and capture. Native tests confirm interrupted dwell and two-second completion for both pawn types. Update the role instruction, contextual prompt and ring duration together; no additional HUD card.
3. Render the complete host/client experience. The first headlight beams washed out the road. Lower each lamp from 3200 to 900 lumens and anchor to existing lamp part positions for every appearance. Use the existing synchronized night blend and no extra light shadow maps. Final renders show the relay ring and headlights together with the existing clean role/speed cards.

| Quality | Before | After |
|---|---:|---:|
| Joining-client driving reliability | 2 | 8 |
| Relay interaction clarity | 4 | 8 |
| Night road readability | 5 | 8 |
| Drift responsiveness (provisional) | 6 | 7 |

The drift score is provisional: native parameters and normal speed limits are checked, but automated input cannot establish how satisfying the drift feels to a person. Stronger steering and anti-slide assist are prototype tuning, not validated balance.

## Evidence

- `Saved/Logs/ChaseMP_reentry-red_A.log`: failing remote re-entry regression.
- `Saved/Logs/relay-drift-red-rules.log`: initial steering/slide checks fail.
- `Saved/Logs/ChaseMP_relay-red_Host.log`: driving into a relay does not start activation before the change.
- `Saved/Logs/ChaseMP_reentry-green_A.log`: remote W input, throttle 1 and speed 120 km/h.
- `Saved/Logs/ChaseMP_reentry-night-review_A.log`: rendered re-entry with latency/jitter/loss, throttle 1 and speed 103.3 km/h.
- `Saved/Logs/reentry-final-rules.log`: native RoundRules succeeds.
- `Saved/Logs/ChaseMP_reentry-final-round_Host.log`: two-second car and on-foot dwell, interruption, escape, role swap and recovery.
- `Saved/Logs/reentry-final-package.log`: Shipping archive build succeeds.
- `Saved/Logs/reentry-original-verification.json`: 142 original source/config hashes checked; zero changes.

Packaged re-entry receipt and final executable hash follow below. Screenshots are actual native Unreal renders; no mockups.


Final delivered archive: `Saved/Logs/reentry-final-package.log` — BUILD SUCCESSFUL. Packaged Host/Join/Ready plus client exit/re-entry/W regression passed at 121.18 km/h and throttle 1.00. Receipt: `Docs/evidence/reentry-dwell/packaged-reentry-receipt.json` (4 October, 12:24 MDT). Child Shipping SHA256: `8FB2A656A267549FB1735906CC36A9E64914070997093CECDF8AC7DCC531CF97`. Final native round receipt confirms escape, swapped-role wreck recovery, second-wreck defeat and rematch. Final packaged screenshots were inspected; headlights and the revised relay instruction are visible. Original source/config verification: 142 checked, zero changes.
