# CitixChase gameplay-loop gap audit

Checked 1 October 2026 against `E:\UnrealProjects\CitixChase`. This is a source/log audit, not a new gameplay test. No gameplay files or files in `E:\Citix\Citix` were changed during this audit.

## Finding

The remake has a server-side prototype round loop. It lacks reliable player-facing integration and several rules needed to close that loop. The immediate problem is not absence of all chase code: **the default Chase GameMode never assigns the HUD containing its ready, objective, timer, and result information.**

One connected player intentionally remains in Waiting. Two players must each ready before a round starts. Without visible prompts, even a running chase server can look like ordinary free driving. Existing scripted tests bypass discovery and parts of the real input path, so their success does not contradict the reported experience.

## Normal player path traced

| Step | Current evidence | Assessment |
|---|---|---|
| Load chase mode | `Config/DefaultEngine.ini` selects `/Script/Citix.CitixChaseGameMode`; current `Saved/Logs/Citix.log` reports that class. | Configured correctly in the inspected path. Individual map overrides still need checking when testing. |
| Draw chase instructions | [GameMode constructor](../Source/Citix/Chase/CitixChaseGameMode.cpp#L16) derives directly from `AGameModeBase` and sets no `HUDClass`. Only the older DrivingGameMode assigns `ACitixDrivingHUD`. [Chase panel](../Source/Citix/Player/CitixDrivingHUD.cpp#L270) exists. | Definite integration gap; no native HUD assignment found elsewhere in source. |
| Ready from car | [Vehicle F handler](../Source/Citix/Vehicle/CitixVehiclePawn.cpp#L1256) calls `ServerChaseInteract` in Waiting/Results; server calls [BeginInteraction](../Source/Citix/Chase/CitixChaseGameMode.cpp#L87). | Input reaches authority. Exactly two players and two ready entries are required. No replicated individual ready flags or native host/join screen found. |
| Countdown/pursuit | [StartRound/Tick](../Source/Citix/Chase/CitixChaseGameMode.cpp#L81) set 10 seconds then 300 seconds. | Timers exist; no chase-phase input lock was found in vehicle/foot controls. Players can move during countdown/results. |
| Exit and activate relay | [On-foot F](../Source/Citix/Character/CitixOnFootPawn.cpp#L389) sends begin/release RPCs. Relay timer is three seconds on server. | Hold route exists. Controller E also starts an interaction, but has no release binding. Shared server interaction slot can be overwritten by the other player. |
| Escape | Three relays unlock two beacons; runner vehicle proximity ends round. | Underlying transition exists; active target identity, functioning car/ownership, and visible destinations need acceptance checks. |
| Ram/wreck | [Collision handler](../Source/Citix/Vehicle/CitixVehiclePawn.cpp#L1303) computes closing speed and calls [TryRam](../Source/Citix/Chase/CitixChaseGameMode.cpp#L88). | Role/speed/cooldown/separation checks exist. Real physical rams have not been proven by the inspected harness. |
| Replacement/capture | First wreck creates two cars and protection; entry has reserve checks; capture has range/sight checks. | Recovery and capture paths exist, with ownership, safe-ejection, and target-state gaps below. |
| Round two/rematch | Results countdown starts round two; FinishMatch compares player scores. | Results also accepts rematch readiness after round one. Disconnect does not clear all match state. |

## Ranked missing or incomplete work

| Priority | Gap and consequence | Required next action |
|---|---|---|
| P0 | Chase HUD is not assigned; players cannot reliably discover ready, roles, objectives, or wins. | Assign existing HUD and make lobby/round/status information prominent. Verify both rendered clients. |
| P0 | Host/join is console/launch-argument based; no native connection flow or individual ready feedback found. | Add a minimal host/join/ready surface using Unreal travel, with connection failure and full-session messages. |
| P0 | One `InteractionController` serves both players; relay completion selects any nearby unfinished relay rather than a pinned target. E starts without release cancellation. | Per-player holds, pinned targets, complete cancellation, and one chase input path. |
| P0 | Round and match results share one phase. F during round-one results can restart a match before role swap. | Distinguish round results and match results; accept rematch only after the second round. |
| P1 | Traffic takeover remains available before runner wreck and to the chaser; generic nearest-car selection does not filter all eligibility first. | Restrict chase entry to own functioning car or the eligible runner reserve. Keep traffic as disruption only. |
| P1 | Second wreck ends round while character health stays 50. Forced ejection tries three offsets without a ground check or guaranteed clear fallback. | Apply the second 50-point loss; validate grounded clearance and handle exhausted candidates without spawning into geometry. |
| P1 | Capture does not require an on-foot runner, and does not revalidate chaser pawn/role throughout the hold. | Enforce the intended on-foot capture phase continuously; prevent stale holds after possession changes. |
| P1 | Disconnect resets phase/status but leaves round number, score, objectives, reserve cars, and interaction presentation partly intact. Third-player admission is not rejected. | Central cancellation cleanup and server two-player admission limit. |
| P1 | Placement only projects fixed coordinates onto drivable edges. Two reserve points cannot establish citywide safe walking access. | Use the existing connected road graph plus clearance/walking checks; measure spawn travel and reserve coverage. |
| P1 | Existing city-seed mismatch checking casts to `ACitixGameState`; Chase GameState derives directly from `AGameStateBase` and exposes no city identity. | Replicate chase city seed/config identity and reject incompatible clients before ready. Retain deterministic local generation. |
| P1 | Reveal is a ring/arrow on the chaser car, rather than runner outline. It searches pawn PlayerState, which needs checking for unpossessed cars and remote/on-foot targets. | Use a replicated current runner pawn reference; show reveal on foot and in cars, plus time to next reveal. |
| P2 | Chase panel has overlapping reveal/hold text and inherited HUD surfaces. Old sandbox input contexts and descriptions remain. | Refine hierarchy and contextual feedback; disable unwanted gameplay inputs without changing driving controls. |
| P2 | Build/package logs are historical. Shipping launcher, normal-control matches, adverse latency, and paired-player balance are not established. | Fresh build/package and acceptance evidence after integration. |

## Existing work to preserve

- Vehicle reset already has a five-second input cooldown. `UCitixVehicleMovementComponent::ResetVehicle` rights/repositions without repairing hull. Test it; do not rewrite it speculatively.
- Ram separation is latched until the previous pair moves apart; the pure rule test includes a held-contact rejection.
- First-wreck character health and relay progress are stored separately. Keep relay progress through recovery.
- Beacon visibility/material and movement replicate. Verify normal clients see them before replacing their implementation.
- Keep city generation, traffic, vehicle physics, braking, drifting, boost, cameras, sound, and normal walking intact.

## Evidence boundary

`ChaseMP_test6_Host.log` records scripted escape, role swap, first wreck, reserve entry, second wreck, scoring, and rematch. `ChaseMP_cap3_Host.log` records capture. The harness teleports pawns, completes relays directly, and calls `TryRam` with 60 km/h; these are state-machine checks, not player-control acceptance tests. Existing smoke-test script also stops every process named Citix; revise it to stop only processes it created before using it alongside another Citix session.

`CitixChasePackage.log` reports historical packaging success and Shipping artifacts exist. No new compile, package launch, visual inspection, two-PC match, preservation hash comparison, or balance study was performed for this audit.

## Next step and completion condition

Implement Tasks 1–4 of the [playable-loop plan](superpowers/plans/2026-10-01-playable-chase-loop.md) first. Stop feature expansion until two players can discover ready, complete objectives or capture/recovery, see a result, swap roles, and rematch using ordinary controls. Then finish placement and reveal presentation and run the complete acceptance pass.

The inherited `PROJECT_GOAL.md` and initial `CURRENT_STATE.md` sections describe the old sandbox and are not reliable chase requirements. The approved two-player pursuit specification and the plan below take precedence.
