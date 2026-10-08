# LAN, settings and drift review

## Performance controls

The intended qualities are independent saved controls, immediate honest feedback, comfortable mouse/keyboard input, and a readable ESC card at 720p and 1080p.

### Verified behavior

`Saved/Logs/LANSettingsDrift-Red.log` recorded the original regression: selecting a graphics preset reset the selected 144 FPS cap and replaced manual 63% scene resolution. Automatic also reset the cap.

Fresh `Saved/Logs/LANSettingsDrift-Green.log` records success for:

- `Citix.Performance.PresetPreservesIndependentControls`
- `Citix.Performance.ScaleRangeAndRuntime`
- `Citix.Performance.FPSDetents`
- `Citix.Performance.SavedControlsRestore`

These cover real render-percentage CVar values at 30%, 100% and 110%, subsequent normal settings application, one-percent rounding and bounds, all six FPS runtime values, a fresh settings-object reload, preset independence, and Automatic preserving the chosen cap.

The saved resolution override remains separate from UE scalability, whose standard resolution setter clamps at 100%. Slider dragging changes only the relevant runtime CVar; mouse capture completion or keyboard release writes configuration. Closing the menu also flushes pending input.

### Scoped refinement

The first review found three weaknesses and corrected them: FPS labels did not align with their six equally spaced detents; the native marker's text alignment displaced the visible mark; selected-preset coloring multiplied a dark brush and had weak contrast. Detent labels now align with the track, 100% has its own marker at the correct position, and selected presets have a brighter normal brush. A further responsiveness pass removed full scalability/audio/HDR application from live slider movement.

The runtime widget probe `VerifyPerformanceControls()` exercises the real slider delegates, logs actual render scale and FPS values, reloads saved controls, reapplies startup settings, checks Automatic, and restores the original settings. `Saved/Logs/ChaseMP_lan_graphics_A.log` and the matching Host log confirm actual 30%, 100% and 110% render values and all six FPS caps in live gameplay. The first combined probe returned FAIL during its remaining reload/Automatic checks. Engine-source inspection confirmed a real widget bug: UE 5.8's programmatic `USlider::SetValue` broadcasts `OnValueChanged`, so refreshing the displayed recommended scale inadvertently created a manual override. A guarded refresh now suppresses only those synchronization callbacks; real user/delegate input still applies and saves. The probe retains its original metadata assertion and adds actual runtime percentage plus per-checkpoint logs. A fresh execution must verify the fix.

The first 1280x720 capture exposed a clipped footer; increasing the card's logical height from 650 to 690 fixed it. Fresh `GraphicsPreset-1-A.png` captured at 20:50:28 shows the entire footer, consistent spacing, readable main controls, aligned detents and the native marker. The selected MEDIUM preset shows 85% AUTO while preserving 240 FPS, confirming that refresh no longer creates a manual scale override. `ChaseMP_lan_graphics_fixed_A.log` also records preset LOW/MEDIUM/HIGH runtime values of 70%/85%/100%, manual override -1 and PASS. Supporting copy remains small at 720p but is secondary to the controls. No unrelated visual effects were added.

### Provisional scores

- Independent saved controls: 8.5/10, backed by fresh automation; process restart/map-travel evidence still pending.
- Immediate and truthful runtime feedback: 8.5/10, backed by CVar automation; packaged widget execution still pending.
- Input comfort: 7/10 from live widget-delegate execution; direct physical mouse/keyboard input remains unverified.
- Visual readability/coherence: 7/10 before the footer correction; 8/10 on the fresh corrected 720p capture. A fresh 1080p review remains pending.

## LAN lobby screenshot review

Reviewed `Saved/Screenshots/LANNearbyGames-A.png` at 1920x1080. The named room, 1/2 occupancy and Join action clearly communicate the discovery outcome. Host and Find Nearby Games remain recognizable, and Advanced is collapsed.

Current scores: discovery clarity 8/10; hierarchy 7/10; visual coherence 8/10. The strongest potential improvement is a quieter, compact Advanced disclosure so the room and Join action form the focal point. Keep discovery status next to the results, rather than below Advanced. The card also has approximately 110 pixels of unused bottom space; sizing it closer to its content would look more deliberate. The ESC Settings hint currently appears informational; its interaction affordance should match whether it is clickable.

This screenshot proves a local UI state only. It does not prove discovery or joining between two separate computers.

The second fresh `LANNearbyGames-A.png` at 1280x720 shows Advanced reduced and dimmed; the room/Join action now has a clearer focal point. Revised hierarchy and coherence score: 8/10. The roomy lower card remains a minor composition trade-off; further changes would not improve the joining flow enough to justify another redesign.

## Drift response and physics self-review

The intended qualities are a visibly sharper handbrake turn, less travel in the old forward direction, controlled release/countersteer, and preservation of ordinary role handling, speed limits and Ice Wave. A real street-corner visual review is still separate from the isolated physics evidence below.

### Fresh behavior evidence

`CitixChase.Drift.PhysicsResponse` passes in `Saved/Logs/LANSettingsDrift-Final.log` (2026-10-06 02:42:23 UTC). The test creates an isolated game world with a static floor and the actual `ACitixVehiclePawn`, applies each production chase role configuration, and advances the real Chaos scene and movement component. It settles suspension first, starts at 3,000 cm/s (108 km/h), then holds full steering and handbrake for 0.8 seconds. These are simulated body measurements, not assertions about tuning constants.

The original code first failed the 45-degree heading and 20-metre old-forward travel acceptance checks in `Saved/Logs/LANSettingsDrift-Red.log`. Fresh paired measurements disable the new yaw-response tuning to restore the original handbrake steering/handling path on the same body and floor. This provides a repeatable before/after comparison rather than comparing unrelated drives.

| Role / tick rate | Heading before → after | Old-forward travel before → after | Nose/velocity slip before → after |
|---|---:|---:|---:|
| Chaser / 30 Hz | 38.95° → 66.81° | 22.00 m → 19.39 m | 25.54° → 17.00° |
| Chaser / 60 Hz | 38.78° → 66.09° | 22.03 m → 19.53 m | 25.32° → 16.54° |
| Chaser / 120 Hz | 38.48° → 65.64° | 22.05 m → 19.61 m | 25.07° → 16.31° |
| Runner / 30 Hz | 41.33° → 67.24° | 21.23 m → 18.75 m | 24.95° → 15.79° |
| Runner / 60 Hz | 41.32° → 66.64° | 21.26 m → 18.88 m | 24.80° → 15.42° |
| Runner / 120 Hz | 41.31° → 66.32° | 21.28 m → 18.96 m | 24.73° → 15.23° |

At 60 Hz this is approximately 70%/61% more heading rotation for Chaser/Runner, 11% less old-forward travel for both, and 35%/38% less nose/velocity slip. Heading varies by only 1.18° for Chaser and 0.91° for Runner across the three tested tick rates. Left turns mirror right turns in every tested case.

The same fresh test verifies:

- Peak planar speed stays at or below its initial 3,000 cm/s in every main turn; low/high initial speeds of 800 and 6,000 cm/s also gain no speed.
- Releasing both steering and handbrake produces only 7.88°/7.27° additional yaw over the next 0.4 seconds at 60 Hz. Releasing steering while retaining the handbrake produces 16.27°/17.22°, below the unchanged 20° acceptance limit.
- Countersteering reverses yaw; final Chaser/Runner angular rates are -100.78°/s and -95.92°/s at 60 Hz.
- Normal-driving role yaw and travel match disabled-assistance controls within 0.1° and 0.1 cm.
- Actual Pursuit GameState and PlayerState freeze checks, with throttle and boost held, match original frozen yaw/travel. Frozen end speeds are 1,082.99 and 932.91 cm/s, below the 1,500 cm/s test cap.
- Airborne cars receive zero yaw assistance and no planar nose alignment, even with initial sideways momentum. Gravity remains active (vertical velocity -828.36 cm/s after the measured turn).

Assistance checks fresh wheel contacts rather than last frame's grounded flag, requires at least two contacts, useful forward speed and an upright chassis, and excludes frozen/destroyed bodies. Exact time-based planar rotation and nonnegative exponential speed scrub preserve vertical velocity without injecting planar speed. Angular X/Y remain untouched. New assistance is enabled in chase role configuration; ordinary steering, engine output, role speed limits, braking, boost and gameplay rules retain their existing tuning. Existing lateral stability assistance now also requires actual ground contact.

### Review and refinement

The first working pass rotated both bodies approximately 63–64 degrees, but product review identified three meaningful weaknesses: steering release with the handbrake still held retained too much yaw; Runner's 120 Hz momentum improvement narrowly missed the 10% acceptance criterion; and frozen/airborne/ordinary handling needed behavior evidence beyond code inspection.

The highest-impact change was stronger, smoothly fading yaw cancellation on steering release. Entry gain remains unchanged, avoiding a sharper entry snap. A small increase in the bounded yaw target (95 → 100 degrees/s) and alignment rate (4 → 4.5/s) improved corner geometry while retaining the same speed cost. Real frozen and normal-role comparison scenarios, sideways-airborne exclusion, low/high speeds, and runtime guards against negative scrub/alignment tuning completed the verification. No acceptance check was weakened; the frame-rate heading tolerance was tightened from 12° to 5°.

After that refinement, the complete drift automation passes. Scores for the observed component behavior are sharper turn response **8.5/10**, momentum/nose alignment **8/10**, release and countersteer continuity **8/10**, and preservation of tested ordinary/frozen/airborne behavior **9/10**. The 11% travel reduction is useful but not transformative; a held-handbrake release still has deliberate residual rotation. Those remaining trade-offs are appropriate for a drift rather than an instantaneous direction change. Camera, smoke, road collisions and subjective tight-corner feel remain unscored until a fresh in-game visual drive is observed. Further feature additions would not improve this scoped response.

## Integrated product review and validation

Three refinement passes focused on the intended experience rather than adding features:

1. Strengthened turn/release behavior using paired real-physics measurements; reduced Advanced's visual weight and aligned slider markers/labels.
2. Corrected automatic-resolution reentry in UE 5.8, made the settings footer fit at 720p, rechecked two-player capacity at Login, and deferred pending-driver cleanup after network failure callbacks.
3. Cleared stale room rows on failure, isolated local probe settings files, and checked the packaged lobby, full-room rejection and host departure. A shared-file probe failure was kept in the evidence, then an isolated packaged runtime probe passed.

The single most valuable finishing change was making failure recovery retain a useful error and usable Retry/Cancel actions after world travel. The initial timeout test crashed when its callback immediately destroyed the pending driver; the repeated test now returns to a usable lobby without a crash.

| Intended quality | Before refinement | Reviewed result |
|---|---:|---:|
| Simple named LAN room discovery | 6/10 | 8.5/10 |
| Clear connection and failure feedback | 4/10 | 8/10 |
| Readable independent performance controls | 6/10 | 8.5/10 |
| Sharper drift with less old-forward travel | 6/10 | 8.5/10 |
| Preservation of tested gameplay behavior | 7/10 | 9/10 |

The strongest remaining limits are environment validation and subjective driving feel. Two local processes prove the local session flow, but do not prove broadcast/firewall behavior on two physical computers. Real-device LAN testing remains outstanding. The isolated physics measurements show the requested turn and alignment improvement; live human judgement of street-corner feel remains useful. These limits are not concealed by the scores.

### Evidence retained

- All 12 Citix automation tests pass, exit 0: `Saved/Logs/LANSettingsDrift-FinalVerified.log`. Covers real NULL session creation/search/cancel, metadata/admission, actual Login capacity recheck, saved controls, real screen percentage including 110%, all FPS detents, drift physics, existing Ice Wave and round rules.
- Packaged named-room discovery/join: `Saved/Validation/LAN/PackagedLANConnected-A.json` (two players); actual room screenshots captured at 720p and 1080p.
- Packaged isolated performance widget probe: `Saved/Validation/LAN/PackagedGraphics-Solo.json` passed. Exercises real slider delegates, presets, reload/initialization, Automatic and ESC input restoration. The isolated run used NullRHI; rendered menu layout was separately inspected.
- Actual third-player rejection: `Saved/Validation/LAN/RoomFull-B.png`, readable full-room feedback plus Retry/Cancel.
- Actual host departure: `Saved/Validation/LAN/HostDeparture-A.png`, readable network timeout and usable lobby recovery. The departed host was an owned test process.
- Different version: `Saved/Screenshots/LANVersionMismatch-A.png`, named room remains visible but Join is disabled. Admission also rejects incompatible versions.
- Shipping build/staging/archive: `Saved/Validation/LAN/ShippingBuild.log`, exit 0. Delivery is separate in `PackageLAN`; existing `PackageIceWave` remains untouched.

The first packaged rule test combined competing ready drivers and did not produce a fresh completion receipt; its old receipt is not counted. The independent two-process round regression uses the existing scenario driver as sole Ready owner. Final results are recorded below after completion.

### Final fresh results

The final shipping binary and archived binary have matching SHA256: `651F69386EA53773417957003C1F9E066AD7004E6F1CAF46EB8FD8D72D6EF758`. The final `lan_delivery` packaged probe used independent GameUserSettingsINI profiles. Host and guest both wrote fresh PASS receipts at 2026-10-06 03:00:39 UTC; the guest's fresh LAN receipt reports two players. This verifies the rendered multiplayer settings flow, persistence, automatic restoration, input restoration and match-not-paused checks without the earlier shared-file interference.

Fresh two-process gameplay regression `Saved/Logs/ChaseMP_lan_round_regression_Host.log` contains no FAIL results and explicitly passes countdown/Pursuit, five relays and exit unlock, first escape, second-round role swap, first wreck/ragdoll, rejection of replacement before 40 seconds, second-car destruction, and rematch restart. The current `Saved/ChaseTest-wreck.json` is complete. Station and pistol re-entry opt-in scenarios were not requested in this run; their receipt booleans are not counted as verification. This regression uses the direct-address headless fixture; named-room discovery/Join/Ready/Pursuit are covered by the separate rendered packaged run.

No internet/EOS room-code support is included. No two-computer LAN claim is made. All owned test processes were cleaned up; independently launched games were preserved.
