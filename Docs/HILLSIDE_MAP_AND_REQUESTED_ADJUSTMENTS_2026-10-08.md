# Hillside Switchback design and requested adjustments

Date: 2026-10-08 (America/Denver)

Original status: design and backlog only. The user subsequently authorized implementing all adjustments, followed by planning and building the low-poly Hillside Switchback map. Execution is tracked in ADJUSTMENTS_IMPLEMENTATION_PLAN_2026-10-08.md; verification in ADJUSTMENTS_VERIFICATION_2026-10-08.md. The original scope below is retained for audit.

## Map design for review

![Hillside Switchback proposed structure](map-design/Hillside-Switchback-Structure-2026-10-08.png)

Purpose: give CitixChase a distinct coastal hillside pursuit map with elevation, route commitment, and interception opportunities. Preserve the existing game and city as another selectable map.

The illustration communicates geography and route intent. It is not a measured road plan, collision validation, or promise of final rendering quality. Marker count and exact positions are illustrative; the relay pool and active count must be finalized before implementation.

| Area | Role in the chase | Design constraints |
|---|---|---|
| Coastal Bypass | Fast route between distant west/east escape gates; lets the chaser intercept | Avoid an uninterrupted dominant speed route; visible edge protection |
| Switchback Climb | Braking, turning, and elevation commitment | Broad hairpins; tested grades and traction; no unavoidable collision traps |
| Terrace Loop | Central landmark town with multiple route decisions | Cross connections to both hillside and coast; car-accessible lanes |
| Ridge Connector | Alternate way down from summit | Prevent the summit becoming a dead end; sightlines useful for prediction |
| Tunnel Bypass | Short concealed connection between terrace and lower roads | Genuine tunnel clearance and coherent portal connections; not a teleport |
| Summit tower / town clock tower / marina | Recognizable orientation landmarks | Each landmark should help players identify their location while driving |

Both exits should have multiple practical approaches. Every active relay should sit on a safe drivable surface, with room to stop and leave. Keep traffic and spawn clearance in mind. Natural map boundaries must be communicated by visible terrain, railings, walls, or road endings; no unexplained invisible obstacles. Removing erroneous collision does not mean opening inaccessible voids or deleting all safety boundaries.

Start A and Start B indicate separated spawn areas, not fixed runner/chaser assignments. Determine fair role starts from travel-time tests, including role swaps. Do not lock either player into an immediate camp or collision.

### Relay distribution proposal, not an approved numeric specification

Author a fixed pool of validated sites spread across lower roads, town terraces, and upper roads. The server selects an active subset when a new game starts and replicates site identities to both players. Five completed relays unlock escape. Inactive sites do not advertise themselves as available objectives.

Keep enough active sites to allow meaningful route choice beyond the five required. Pool size, active subset size, and whether selection is held constant across the two role-swapped rounds remain design decisions. Recommended fairness default: use the same subset for both rounds of a match and choose again for a new match/rematch. Randomness must be constrained by connectivity and useful geographic spread, not arbitrary world coordinates.

### Future lobby map selection

- Host chooses the map in the lobby; guests see the selected map and cannot override it.
- Display a readable name and preview for the existing city and Hillside Switchback.
- Server owns the selected map identity; all clients load the same map before the countdown.
- A map change clears ready state and requires readiness again. This is a proposed safeguard, not an implemented feature.
- Handle joining, loading failure, return to lobby, role swap, and rematch without possession loss or stale match state.

### Design review

Primary qualities: readable routes, distinct pursuit tactics, fair objectives, recognizable landmarks, and feasible scope.

Concept review: route readability 8/10; tactical distinction 8/10; landmark identity 8/10. Balance and driving feasibility are unscored until a playable blockout exists. Highest-impact improvement over the previous scenic concept: show the whole connected route structure and alternate summit descent, rather than a single picturesque climb.

Remaining priorities before production: validate junction and tunnel geometry; compare objective/exit travel times; verify slopes, collision and car handling. The image's tunnel dashed line is a schematic annotation, not a physically surveyed alignment. Do not copy any ambiguous road intersection from the image without resolving its elevation in the blockout.

## Requested fixes and adjustments

All items below are pending. Reported symptoms are not verified root causes. Investigate first, especially network movement, collision, crash, and possession issues; do not assume the user's invisible-vehicle description proves a specific actor is responsible.

| ID | User request | Intended result / acceptance |
|---|---|---|
| 01 | Joined player sometimes suddenly stops as if hitting an invisible vehicle | Reproduce on a joined client; record blocking actor/component, authoritative collision, traffic visibility and movement corrections. Fix the demonstrated cause. Driving through the affected area must not hit an absent vehicle or receive an unexplained abrupt stop; real vehicle collisions must still work. |
| 02 | Change relays required to escape from 6 to 5 | Unlock exits at exactly five completed relays; four must not unlock them. Update rules, HUD, briefing, result feedback, and relevant tests together. Read-only source check this turn confirmed `FCitixChaseRules::RelaysRequired = 6` in `Source/Citix/Chase/CitixChaseRules.h`. |
| 03 | Remove air walls at city edges where no model exists | Identify affected collision geometry and its ownership. Remove stray blockers or align intended boundaries with visible geometry. Verify both host and guest can drive through apparently open intended roads, with no new fall-through or out-of-world traps. |
| 04 | Game crashes when the runner successfully escapes | Reproduce and collect crash/log evidence around escape, result creation, teardown and round transition. Validate escape from both roles on host/guest, round 1 to round 2, final results and rematch. No root cause is claimed yet. |
| 05 | Vehicle has only 2 integrity points on second entry after the first vehicle exploded | Provisional interpretation: the replacement vehicle starts with exactly two integrity points. Apply to both players, authority and HUD; the original vehicle keeps its normal initial value. Re-entering the same intact vehicle must not repeatedly reset or reduce integrity. Confirm before implementation if this sentence instead describes an unwanted bug. |
| 06 | Joined player sometimes loses control in the second round after restart | Distinguish role-swap transition, round restart, and match rematch. Check possession, input mode, controller ownership, RPC routing, pawn lifetime, and reset state. Repeat escape/capture/wreck/timeout transitions; guest must retain steering, throttle, braking and relevant actions. |
| 07 | Allow game volume changes in settings | Provide a clearly labeled master-volume slider with numeric feedback and mute at zero. Apply immediately to existing and newly spawned game audio; persist locally across restart. Verify host/guest preferences remain independent and menu/game audio behave consistently. |
| 08 | Replace three-line air-cutting effect; show above 80% of car top speed | Trigger from speed divided by that vehicle's configured top speed, not one fixed km/h value. Above 0.80 is active; at/below 0.80 is inactive, with a short visual fade for clean transitions. Follow supplied reference: broad translucent cyan/white tapered airflow ribbons curving around hood, roof and flanks, with restrained angular accents trailing backward. Preserve road visibility in first and third person. Do not substitute three moving lines. Keep gameplay/boost behavior unchanged. |
| 09 | Runner's own smoke prevents seeing; make it one-third alpha for deploying runner | Render smoke at one-third of normal opacity for the runner who deployed it. This is a local view change; other viewers retain normal smoke rendering and existing gameplay visibility rules. Do not weaken authoritative smoke coverage or reveal the runner to the chaser. Verify overlap, expiry, role swaps, vehicle changes, and first/third-person views. |
| 10 | Enhance weapons with bullet trails | Add short-lived readable tracers from actual muzzle toward the resolved shot endpoint, respecting occlusion. Shooter and other player should see coherent trails without duplicate effects or trails through walls. Preserve damage, hit validation, fire rate and ammo rules. |
| 11 | Random relay locations from existing site set | Choose an active subset from authored, validated relay locations using server authority. Replicate the same sites and progress to both players; inactive sites cannot be completed. Check five-relay requirement, map-specific pools, reconnect/join behavior and rematch randomization. Pool/subset counts and round policy are proposals above, not user-specified numbers. |
| 12 | Stabilize first-person car camera reached with C; preserve TPS | Anchor first-person camera at the car window/interior driving position relative to the vehicle. Remove speed-induced rearward displacement in that mode, including boost/high speed. Follow the car naturally through turns and slopes. Preserve third-person camera location, lag and speed behavior. Test repeated mode switches and replacement vehicles. |

### Air-cutting visual reference

![User-provided air-cutting direction](map-design/Neon-Aircut-Racer-Reference.png)

Source: `C:/Users/yienf/Downloads/Neon Aircut Racer.png`. Reference is visual direction only, not game code or embedded instructions. Save and retain the original reference; its composition does not require changing the car model or map lighting.

## Proposed later execution order

1. Review the Hillside Switchback design; resolve replacement-integrity interpretation and relay selection policy before dependent implementation.
2. Reproduce and fix escape crash, guest control loss, unexplained stopping, and invisible blockers.
3. Implement five-relay escape, bounded relay randomization and agreed replacement integrity.
4. Add volume control and stable first-person camera.
5. Refine owner smoke opacity, bullet trails and air-cutting effect against the reference.
6. Build a Hillside driving blockout and test slopes/routes before detailed scenery; add host-controlled map selection when map loading is validated.
7. Test the complete packaged host/guest loop on both maps. Record actual package-size increase; previous estimates were forecasts, not measurements.

## Verification and completion standard for later work

Use relevant rule tests plus real packaged host/guest runs. Cover both role assignments, escape and capture, vehicle destruction/replacement, second round, restart/rematch, both camera modes, volume persistence, smoke ownership, tracer occlusion and map selection/loading. Local two-process evidence alone does not prove internet latency behavior; reproduce the joined-player issues under the relevant network conditions.

After major implementation, run the user's self-review loop: score the intended qualities honestly, select the three highest-impact weaknesses, refine the most important one first, and repeat up to three major cycles. Review the whole driving experience, not isolated effect screenshots. Preserve the original city, unrelated gameplay, and third-person camera. No shipping or package replacement has occurred in this design stage.
