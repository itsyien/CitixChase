# Hillside Switchback Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox syntax for tracking.

**Goal:** Build the complete playable low-poly coastal hillside pursuit map, retain the existing city, and let the host choose between them in the lobby.

**Architecture:** Add an authored elevation-aware map layout and a generation branch in the existing city generator. Reuse the car, chase rules, instanced surface palette, objective projection and traffic systems. Replicate selected map identity and generation revision; rebuild local worlds only from the authoritative selection, clear readiness on changes, and require matching map identity before countdown.

**Tech Stack:** Unreal Engine 5.8 C++, existing procedural instancing; collision-capable procedural terrain/road geometry if existing primitive collision cannot produce continuous graded surfaces.

**Spec:** Docs/HILLSIDE_MAP_AND_REQUESTED_ADJUSTMENTS_2026-10-08.md and Docs/map-design/Hillside-Switchback-Structure-2026-10-08.png.

## Global Constraints

- Low poly. Reuse the established palette, facade vocabulary, lighting and vehicles.
- Preserve the original city and all twelve adjustment requirements.
- Host decides the map. Guests see the selected map and cannot override it.
- Five relays unlock escape. Select six active sites from a validated authored pool; preserve the selection through role swaps and reroll on rematch.
- Genuine elevation, continuous tunnel and connected routes; no teleport substitutes or flat city with hillside decoration.
- Preserve preexisting workspace changes and installed package. Measure final package growth using equivalent build configurations.

## Review Focus

- Every car profile must climb, turn and stop at objectives without bottoming out or snagging segment seams.
- Client generation, loading and map changes must not leave old colliders, stale objectives or invalid possession.
- Both exits and summit must have multiple practical approaches; traffic must not block narrow hairpins permanently.
- Map silhouette and landmarks must communicate coast, town terraces and summit while driving, not just from an aerial image.
- Slopes, tunnel roofs, water and visible edge protection must agree with dry-surface recovery and replacement parking.

## File responsibilities

- New City/CitixHillsideLayout.h/.cpp: authored three-dimensional routes, objective pool, landmarks, terrain coverage, deterministic revision/hash and pure validation.
- New City/CitixHillsideBuilder.h/.cpp: low-poly terrain, continuous graded roads, tunnel portals/interior, guardrails, marina, town terraces and radio summit using existing materials.
- City/CitixCityGenerator.h/.cpp: selected map identity, generation branch, teardown, map-aware dry footprint and height queries; retain existing generation branch.
- City/CitixRoadNetwork.h/.cpp: zero-default node elevation and a shared three-dimensional edge sampling API; existing city remains at its current elevations.
- Traffic/CitixTrafficSystem.cpp and TrafficVehicle.cpp: elevate actual poses/parking to sampled road surfaces and follow slope orientation without changing existing city behavior.
- Chase/CitixChaseGameMode.*, GameState.*, Player/CitixDrivingPlayerController.*: server-selected identity, validated objective/spawn/recovery layout, readiness reset and client generation acknowledgment.
- Chase/CitixChaseLobbyWidget.*: two readable host-selectable map cards with previews; guest selected-state display and loading feedback.
- New City/CitixHillsideLayoutTest.cpp and runtime map probe: connectivity, grade, footprint/clearance, objective distribution and actual driving route checks.

## Task 1 — Authored layout and road elevation

- [ ] Write failing tests for connected coast/terrace/climb/ridge/tunnel routes, two exit approaches, summit loop, at least twelve safe relay candidates and two separated starts.
- [ ] Run the tests and confirm missing layout/elevation fails.
- [ ] Author a roughly 750 by 650 metre playable footprint with approximately 60 metres of elevation. Keep primary grades at or below 12%, hairpin centreline radius at least 24 metres, ordinary two-way road width at least 13 metres, and tunnel clearance at least 7 metres wide by 5 metres high per direction. Validate sampled curves, not merely their endpoints.
- [ ] Connect coastal bypass to both sides of the terrace loop, switchback climb to summit and ridge descent, and tunnel to lower road and terrace. Include bends/intersections on the coast to avoid one dominant straight route.
- [ ] Add height-aware edge sampling with zero elevation defaults. Run existing road/route tests and the new layout tests.

## Task 2 — Complete low-poly world and collision

- [ ] Add a failing real-world surface test at route grades, hairpin seams, tunnel portals, objective lay-bys and map boundaries.
- [ ] Build faceted terrain, continuous drivable roads and intersection pads. Keep terrain clear of tunnel road volume; use visible portal walls/roof and interior lighting. Use procedural collision only where primitive surfaces cannot satisfy continuous geometry.
- [ ] Build distinct marina, stepped town terraces/clock tower and summit radio mast, with restrained lane markings, directional signs, lights and guardrails. Place visible protection where a road abuts a dangerous drop; avoid unexplained empty bordered areas.
- [ ] Implement map-aware height/dry footprint queries and collision teardown. Test actual traces and sweeps against geometry, including river/coast exclusions and safe recovery poses.
- [ ] Adapt traffic/parking elevation and slope poses; test visible and pooled traffic at coast, terrace and summit. Do not disable the established traffic system to avoid adaptation.
- [ ] Render aerial plus coast, town, summit and tunnel driving views; review silhouette, orientation cues, road readability and performance.

## Task 3 — Server-owned selection and playable chase

- [ ] Add failing checks for unauthorized guest selection, readiness reset, map identity mismatch and objective reachability.
- [ ] Implement host selection during waiting/result lobby state only. Replicate map ID and revision, rebuild authoritative/local worlds in a controlled transition, remove old objectives/vehicles/colliders, restore a safe lobby pawn and reapply local audio settings.
- [ ] Require both clients to acknowledge matching map ID, revision and layout before readiness/countdown. Handle joining while loading, failure feedback, return to lobby and rematch without stale state.
- [ ] Use authored hillside spawn/relay/exit/recovery pools. Select six geographically distributed relays with five required. Preserve role-swap fairness; test both exit commitments, recovery cars and every objective approach.
- [ ] Build polished two-map cards in lobby with clear name/preview, selected highlight, host-only controls, guest feedback and loading state. Verify keyboard/gamepad and 1280x720 layout.
- [ ] Run real keyboard host/guest rounds on both maps under latency/loss, including map switching and rematch; inspect possession and suspension evidence.

## Task 4 — Product review and package proof

- [ ] Drive complete coast/climb/ridge/tunnel/terrace routes with multiple vehicle profiles; verify gradients, hairpins and portal joins through actual controls rather than teleported checkpoints.
- [ ] Review the whole experience using the user's five-phase self-review loop, at most three major cycles. Score low-poly coherence, route choice, driving readability, multiplayer trust and lobby clarity. Fix the three highest-impact problems before adding decoration.
- [ ] Build/package both maps. Run packaged host/guest selection, both roles, escape, wreck/recovery, return-to-lobby and rematch. Confirm all twelve adjustments against explicit evidence.
- [ ] Measure equivalent baseline/current package sizes and report incremental map bytes separately from unrelated build differences. Update verification document and checklist, retaining failure evidence and disclosed fixture limitations.
- [ ] Mark the full goal complete only after the requirement-by-requirement audit proves the map and every adjustment.
