# Clearer Pursuit, Recovery and UI implementation ledger
Authority: approved user plan, 2026-10-03. Visual target: second concept, Signal Grid.
Original E:/Citix/Citix is read-only; source/config hashes saved for final comparison.
Restoration: Saved/Backups/clear-pursuit/Source. The remake is already a separate non-Git copy; no Git worktree is needed. Cost: restoration is file-based.

## Implemented
- Shared replicated recovery deadline, pistol hits, ammo/refill. Server-owned entry transaction and four-hit victory; no pistol character-health damage.
- Role speed ceilings, smooth acceleration curve, canonical chase wheels/mass/grip/damping and red chaser paint.
- Shared dry/bridge geometry, ground/clearance query used by positioning paths; last-dry vehicle recovery.
- Timestamped traffic interpolation with 100 ms delay/extrapolation ceiling; shared 270-second lighting clock.
- Road arrowheads and exact road endpoint projection, yellow station pointer, recovery-car routing.
- Native lobby buttons/address/slots/readiness/failure handling; compact graphic HUD and shot feedback.

## Evidence so far
- RED rule test: old chaser limit failed. GREEN: clear-green-rules.log.
- RED same-road route regression: clear-route-red.log. GREEN: clear-route-green.log.
- Multiplayer recovery (80 ms lag, 20 ms jitter, 3% loss): ChaseMP_clear_recovery_Host.log passed first wreck, early entry rejection, 40-second claim, second wreck, role swap/rematch.
- Four pistol hits at character health 50: ChaseMP_clear_pistol_diag_Host.log; fourth wins. Remote aim test corrected without weakening validation.
- Native lobby first rendered blank: widget-tree creation was too late. Fixed in RebuildWidget; SignalGridLobby screenshots show native card and dry road starts.
- Actual Host/Join/Ready widgets activated through Slate focus/Enter: ChaseMP_clear_buttons_key_Host.log and A.log; pursuit starts. This proves widget activation and keyboard accessibility, not a human mouse test.
- Speed probe originally failed: streamed obstacles and unsafe looping contaminated the probe, while body-dependent wheels and excessive damping affected genuine performance. Normalize physics and use validated dry straight-road loops. Current clear_speed_dry probes pass; final measurements pending.
- HUD pursuit exposed null-texture triangle assertion: fixed native default texture; fresh clear_hud_fixed1080 run renders correctly.

## Self-review
Pass 1: correctness and visibility. Core behavior 7, lobby discoverability 4, guidance 6, clarity 6. Largest defects: late-created blank lobby, unsafe initial lobby pawn, route backtracking. Fixed; fresh native lobby/button and route checks pass.
Pass 2: coherence. Readability 7, fairness 7, motion 7, visual coherence 7. Largest defects: speed/boost overlap, guidance competing with bottom status, car bodies affecting performance. Fixed spacing and canonical physics; speed checks underway.
Final review, expanded seed checks, packaged verification, multi-resolution captures, original hash comparison and handout remain pending. Human balance and real two-PC driving/audio feel must remain explicitly unverified.

## Final review and closure evidence
Fresh review by clear_final_review found ordinary exit bypassing shared dry validation; fixed in RequestExitVehicle and verified by CitixSafetyProbe at a bridge/shoreline edge. Follow-up reset probe exposed inherited 400-health sandbox capacities; normalize chase health to 100 while preserving fraction. GREEN runtime safety/boost preservation and native rules now pass.
Native ammo probe passes rate rejection, zero-ammo rejection, unchanged active refill timer and eight-second regeneration. Four-hit sequence passes through a replacement claim/re-entry at character health 50. The fixture needed a client teleport RPC after moving the shooter across the city; the original stale camera was outside pistol range, without any relaxation of shot validation.
Physical speed probe GREEN: twelve of twelve role/body cases reach 190/225, no cap breach. Test-controlled inputs avoid background guest RPCs cancelling boost. Deterministic dry-road loops retain real suspension, ground and forces. This is cruising verification, not human driving feel.
Pass 3: UI clarity 8, guidance 8, recovery 8, visual coherence 7, fairness 6 provisional. Fixed ordinary-exit safety, health normalization and concise connection failure states; reserve final car label and yellow station glyph are clear. Actual 720/1080/1440 screenshots verified; early OS-clamped 1423x889 captures are drafts.
Original hash comparison: 142 tracked source/config files, zero changes. Capture/range interruption, timeout, second exit and disconnect cancellation pass fresh source tests. Traffic takeover boundary and final packaged receipt are the last pending checks.

Final checks closed: traffic takeover rejects 10 km/h, accepts 9.99, retires the traffic source and retains character health 50/full vehicle health. Failed join renders a short message in an intact lobby. Final Shipping archive succeeds; fresh packaged receipt confirms rounds/rematch and pistol re-entry. Replacement F now precedes relay/station holds when the recovery reserve is ready and an eligible car is nearby; the final pistol fixture claims beside a relay through BeginInteraction. No-safe starts require exactly two generated starts and never fall back through Super.
A NullRHI pistol probe had stale remote camera data and missed before the first hit. Rendered source and packaged scenarios pass. The harness now chooses rendering for pistol/camera tests; headless remains suitable for pure rules, recovery and non-aiming scenarios.
Delivery status: implementation, native verification, packaged loop, screenshots, handout and original preservation checks complete. Human two-PC feel/audio and ten-paired-match balance remain unverified; see TWO_PC_PLAYTEST_PROTOCOL.md. No additional feature or dependency was added for polish.


Screenshot feedback update: speed spacing, true glowing chevrons, 13.5 m relay interaction radius and physical yellow drive-through stations are implemented. Five-second free boost and 30% chaser slowdown use the replicated server clock; existing absolute role caps remain enforced. Three refinement passes and fresh native/packaged evidence are recorded in STATION_POLISH_REVIEW.md. The original 142 source/config hashes remain unchanged.
