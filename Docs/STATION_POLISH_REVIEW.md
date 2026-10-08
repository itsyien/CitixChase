# Speed, guidance and breakaway review

4 October 2026. Scope: screenshot fixes and functional breakaway stations. Original E:/Citix/Citix preserved.

## Three review passes

1. Three-digit speed overlap was reproduced in the supplied screenshot. Split the speed, units and boost meter into separate rows and widen the card. Road chevron arms extended beyond their shared tip; move their centres so the arms terminate at the same tip. Replace silent on-foot-only breakaway activation with server-validated drive-through activation; retain the optional on-foot hold. Increase relay interaction radius from 4.5 to 13.5 metres in server checks and HUD prompts.
2. Native renders exposed dark fallback materials: a dynamic material cannot parent another dynamic material. A new native regression check failed before the shader-parent correction and passed afterward. Both arrow glow and station glow now derive from the authored emissive base shader. Keep two station actors relevant to both clients and light them throughout activation; dim spent stations without relighting them when the other station activates.
3. The station initially read as a dark gate from one approach. Add yellow trim to both faces. Reserve label space above the taller speed card. Native 720/1080 and packaged 1440 renders show clear chevrons, readable speed digits and a physical yellow gate. Large glowing relay pillars retain the existing objective style; no extra HUD panel or asset dependency was added.

| Intended quality | Before | After |
|---|---:|---:|
| Speed readability | 5 | 9 |
| Directional guidance | 3 | 8 |
| Station discoverability | 2 | 8 |
| Activation feedback | 3 | 8 |
| Mechanic completeness | 3 | 8 |

Remaining subjective limits: bloom intensity and the prototype 30% slowdown need human chase playtests. Existing driving and balance are not given a proven fairness score.

## Evidence

- Native automation: Saved/Logs/station-release-rules.log, CitixChase.Rules.RoundRules succeeds. Includes shared arrow tip geometry, correct emissive shader, drive-through eligibility, spent-station rejection and relay radius.
- Failing checks before fixes: station-red-rules.log (vehicle activation rejected) and station-glow-red-rules.log (emissive shader fell back).
- Rendered host/client: station1080 native run with 80 ms lag, 20 ms variance and 3% packet loss passes station activation, expiry and relay interruption. Later stationFinal1080 verifies the final visual refinements.
- Packaged actual Host/Join/Ready path: Docs/evidence/station-polish/packaged-round-receipt.json confirms complete wreck scenario and station_passed=true. This checks automatic activation, free boost with an empty reserve, 30% chaser slowdown, reuse rejection and expiry before the round/role-swap/recovery/rematch sequence can complete. The first shorter 1440 run ended before producing a complete-round receipt; the longer 720 run completed it.
- Shipping ignores startup map/IP overrides in UE 5.8 GameInstance.cpp. The test harness now routes every packaged run through the actual lobby buttons. No production startup behavior was weakened.
- Screenshots: Docs/evidence/station-polish/720p, 1080p and 1440p. The 225 readout is a display-layout fixture; the runner's actual ceiling is 190. The 720p-packaged folder uses low-spec rendering for the longer rules check, so it is not the reference for bloom quality.
- Preservation and harness syntax: Saved/Logs/station-delivery-verification.json reports 142 original source/config files unchanged and zero PowerShell parse errors.

The full round receipt precedes the final pointer-padding-only update; gameplay is unchanged by that update. Final Shipping archive and visual smoke evidence are recorded in the handout. Real two-PC latency, audio feel and ten paired balance matches remain human tests.
