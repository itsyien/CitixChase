# Playable CitixChase Loop Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Work in the existing remake; delegation is not required.

**Goal:** Turn the existing chase prototype into a visible, reliable two-player match that starts, offers objectives and recovery, ends fairly, swaps roles, and rematches through normal controls.

**Architecture:** Keep the existing Chase GameMode as server authority, Chase GameState for shared round information, and Chase PlayerState for individual state. Connect the existing HUD and input paths; replace the single interaction slot with one hold per player. Reuse vehicle physics, city generation, native Unreal travel, replicated beacons, and the existing road routing helper.

**Tech Stack:** Unreal Engine 5.8, existing Citix C++ module, Enhanced Input, Canvas HUD, installed UMG/Slate modules, listen-server networking, Unreal automation.

**Spec:** [Approved pursuit requirements and current status](../../CITIXCHASE_PROGRESS_HANDOUT.md), supplemented by the [current source/log audit](../../GAMEPLAY_LOOP_GAP_AUDIT.md). Numerical rules below come from the user's approved two-player pursuit plan; this plan completes that direction rather than introducing a new mode.

## Global Constraints

- Work only in `E:\UnrealProjects\CitixChase`; preserve `E:\Citix\Citix`. Keep the ASCII `Citix` module/project identifiers and UE 5.8 association. There is no Git repository in the inspected remake; do not invent commit steps or initialize one as a prerequisite.
- Keep the existing city script, traffic, steering, braking, drifting, boost, cameras, engine feedback, and on-foot controls. Both roles use identical vehicle performance; chaser sprint is 10% faster. Disable pedestrians and GTA gameplay in the remake.
- Match: exactly two players, two rounds with roles swapped; same city, objective layout, and role-specific spawn positions throughout the match. Most round wins decides the match; a tie stays a draw. Disconnect cancels without awarding a win.
- Round: five minutes; five relays, any three completed by an uninterrupted three-second on-foot hold; two separated escape zones, entered in a functioning owned runner car. Keep completed relays after wrecks. Retain existing 10-second countdown and eight-second round result pause.
- Ram: runner car displayed health 100, damage 25 per accepted chaser ram, closing speed at least 15 km/h, separation and 1.5-second cooldown. Traffic/buildings disrupt physically without health damage; chaser car is indestructible.
- Wreck: first subtracts 50 from character health 100, ejects safely, grants three seconds of capture protection and one fresh reserve car; reserve does not restore character health. Second wreck reaches zero and ends round. Capture is an on-foot chaser hold for two seconds within 2.5 metres and clear sight; the runner must be on foot for this distinct recovery phase.
- Reveal: runner outlined for three seconds initially, then two seconds every 25 seconds, including on foot; runner sees time until next reveal. Spawns roughly 15–25 driving seconds apart with multiple routes. Eligible pursuit areas have replacement parking within approximately 100 metres along validated safe walking routes; highlight nearest two after wreck.
- Defer inventories, upgrades, extra modes, matchmaking services, and power-ups. No new networking or test framework. Do not tune driving to conceal round-rule problems.

## Review Focus

1. Only one driver connected: visible waiting state; ready cannot start a round (Tasks 1, 2, 7).
2. Two actions or an input release in the same frame: each player's hold remains independent; early release and target changes interrupt (Task 3).
3. Wreck beside traffic, walls, or a river: one health loss, clear grounded ejection, reachable reserve, no traffic theft or repair exploit (Tasks 4, 5).
4. Repeated result input, disconnect, or third connection: one award, guaranteed role swap, no accidental rematch, no stale match state (Tasks 2, 7).
5. Remote driver under latency: both clients agree on objective/damage/result, and reveal follows current pawn through car exit/entry (Tasks 6, 8).

## Execution order and files

Complete Tasks 1–4 as the first integration milestone. Use existing console host/join temporarily to test that milestone. Tasks 5–7 complete the requested experience; Tasks 8–9 establish release and balance evidence. Every task includes a reproducible check; a scripted server PASS is not a substitute for a rendered normal-input check.

Existing source paths below are relative to `E:\UnrealProjects\CitixChase`. Expand current compact Chase methods as needed for readability, without restructuring the entire inherited controller or vehicle implementation.

### Task 1: Make the existing loop visible and keep chase inputs focused

**Files:** Modify `Source/Citix/Chase/CitixChaseGameMode.cpp`, `Source/Citix/Player/CitixDrivingHUD.h/.cpp`, `Source/Citix/Player/CitixDrivingPlayerController.h/.cpp`, `Source/Citix/Character/CitixOnFootPawn.cpp`; Test `Source/Citix/Chase/CitixChaseRulesTest.cpp` and rendered host/guest.

**Interfaces:** Consume existing `ACitixDrivingHUD::DrawChasePanel(float, float)` and `ACitixDrivingPlayerController::IsChaseMode() const -> bool`. Produce Chase GameMode `HUDClass = ACitixDrivingHUD::StaticClass()` and a chase-only HUD branch using existing replicated state. Use existing `ServerChaseInteract()` / `ServerCancelChaseInteract()` for F; bind both Completed and Canceled releases on foot.

- [ ] Add editor assertion `CitixChase.Loop.HUDClass`: Chase GameMode defaults must select `ACitixDrivingHUD`. Run it before the assignment; confirm failure.
- [ ] Assign the HUD and draw a chase-specific panel without inherited money/jobs/wanted/weapons panels. Show Waiting and F-ready prompt even before roles are assigned; do not label an unready lobby player RUNNER by default.
- [ ] Stop adding sandbox/time-scrub mappings in chase, including controller E-interaction; check again when replicated Chase GameState becomes available on clients. Preserve car/foot pawn mappings. Reject GTA RPC verbs in chase at authority, not merely through hidden keys.
- [ ] Give role, timer, current win condition, separate car/character health, reserve state, and action prompt separate readable space. Resolve hold/reveal text overlap; display exit-denied messages independently of the absent SandboxDirector.
- [ ] Run the HUD assertion and rendered two-player smoke test: one player sees Waiting; both ready with F; both see countdown, roles, relays, and timer. Press E/J/V/P/Y/fire/shop keys and confirm no chase action or sandbox behavior. Capture lobby/pursuit screenshots.

### Task 2: Close round, result, readiness, and cancellation transitions

**Files:** Modify `Source/Citix/Chase/CitixChaseGameMode.h/.cpp`, `CitixChaseGameState.h/.cpp`, `CitixChasePlayerState.h/.cpp`, `Source/Citix/Player/CitixDrivingHUD.cpp`, `Source/Citix/Vehicle/CitixVehiclePawn.cpp`; Test `Source/Citix/Chase/CitixChaseRulesTest.cpp`.

**Interfaces:** Replace `Results` with distinct `RoundResults` and `MatchResults` in `ECitixChasePhase`; update every caller and harness branch. Replicate GameState `int32 RoundNumber`, `float PhaseSecondsRemaining`; PlayerState `bool bReady`. Add GameMode `void CancelMatch(const FString& Reason)` and `bool CanAcceptDriveInput() const`; override native `PreLogin(const FString&, const FString&, const FUniqueNetIdRepl&, FString&)` for capacity. Keep existing `StartRound()`, `FinishRound(bool, const FString&)`, `FinishMatch()`.

- [ ] Add `CitixChase.Loop.Transitions` assertions: one ready cannot start; two start once; early F during RoundResults cannot reset scores; next round swaps roles; 1–1 is a draw; two final-ready players start a fresh match; repeated FinishRound cannot award twice. Verify failing cases before fixes.
- [ ] Make RoundResults count down visibly for eight seconds, then swap/start round two. Only MatchResults accepts mutual rematch readiness. Replicate individual ready state and round number; preserve fixed player identity for role assignment through the match.
- [ ] Cancel all holds and reveal at results; freeze gameplay movement/actions during countdown/results on the server and clear cached drive inputs. Restore normal vehicle performance unchanged at pursuit. Update `HandleExitVehicle` phase checks without swallowing ready/rematch.
- [ ] Reject a third player, allow only a two-player roster to start, and centralize disconnect cleanup: remove transient cars/holds/protection/damage latches, reset score/round/ready/objectives/timers/reserve presentation, return survivor to Waiting without awarding a win.
- [ ] Run transition assertions and a manual double-F/disconnect test in countdown, pursuit, RoundResults, and MatchResults. Reconnect must produce a fresh round one, not leftover round two.

### Task 3: Make relays and capture true uninterrupted holds

**Files:** Modify `Source/Citix/Chase/CitixChaseGameMode.h/.cpp`, `CitixChasePlayerState.h/.cpp`, `Source/Citix/Character/CitixOnFootPawn.cpp`, `Source/Citix/Player/CitixDrivingHUD.cpp`; Test `Source/Citix/Chase/CitixChaseRulesTest.cpp`.

**Interfaces:** Keep `void BeginInteraction(AController*)`, `void CancelInteraction(AController*)`, and `void CompleteRelay(AController*)`. Replace global `InteractionController` with a controller-keyed map of hold records containing relay index/capture target and server start time. Replicate per-player `bool bInteractionActive`, `float InteractionSecondsRemaining`; introduce `enum class ECitixChaseInteraction : uint8 { None, Relay, Capture }` and per-player `InteractionType`. Consume Task 2 phases and existing `IsCaptureRange(const AController*) const -> bool`.

- [ ] Add `CitixChase.Loop.Holds`: release at 2.9 seconds yields no relay; three uninterrupted seconds yields one; moving to another relay cancels rather than transferring progress; two seconds yields capture; release at 1.9, blocked sight, leaving 250 cm, or active wreck protection prevents capture. Confirm failures on old shared state/target validation.
- [ ] Pin the target on begin; revalidate phase, role, pawn type, target identity, range, protection, and sight throughout the hold and at completion. Reject duplicate begin requests that reset a valid timer. Entering a car cancels the hold. Require both participants on foot for capture.
- [ ] Keep relay progress through wreck; show local hold feedback and the target's capture pressure separately. A relay hold and capture attempt must not overwrite each other's bookkeeping. A failed interaction shows an actionable message.
- [ ] Route normal F press/release/cancel through authority. Remove controller E bypass from Task 1. Do not expose direct relay completion as a client RPC; completed holds are the server's only normal completion path.
- [ ] Run hold assertions and repeat using real F inputs on both clients. Verify moving away, losing sight, possession changes, simultaneous attempts, and completed relay removal. Update scripted tests to distinguish state setup from normal-control evidence.

### Task 4: Close damage, ownership, and recovery loopholes

**Files:** Modify `Source/Citix/Chase/CitixChaseGameMode.h/.cpp`, `CitixChaseRules.h`, `Source/Citix/Player/CitixDrivingPlayerController.h/.cpp`, `Source/Citix/Vehicle/CitixVehiclePawn.cpp`, `Source/Citix/Character/CitixOnFootPawn.h/.cpp`; Test `Source/Citix/Chase/CitixChaseRulesTest.cpp`.

**Interfaces:** Keep `TryRam(ACitixVehiclePawn*, ACitixVehiclePawn*, float)`, `CanClaimReplacement(const AController*, const ACitixVehiclePawn*) const -> bool`, `RequestEnterVehicle() -> bool`, and `ForceChaseEjection() -> void`. Add GameMode `bool CanEnterChaseVehicle(const AController*, const ACitixVehiclePawn*) const` and controller `bool FindSafeChaseExit(const ACitixVehiclePawn*, FTransform& OutExit) const`; use eligibility before choosing the nearest candidate.

- [ ] Extend rule checks with 14.9/15 km/h, 1.49/1.5 seconds, held contact after cooldown, health 100→50→0. Add `CitixChase.Loop.Recovery`: four accepted separate rams cause exactly one wreck; reserve consumption is one; other owner, traffic takeover, destroyed car, and extra reserve are rejected. Run failing runtime cases before changes.
- [ ] Apply the second 50-point health loss before final defeat. Count wreck once and clear stale ram-pair/capture protection state on round start. Keep building/traffic damage at zero and chaser hull unchanged; reject damage outside pursuit or after wreck.
- [ ] Restrict entry to the player's functioning owned car or an eligible unclaimed reserve after first wreck. Filter candidates before nearest selection; claim ownership/reserve atomically on server. Early runner/chaser traffic theft must be impossible. Re-entry never repairs health.
- [ ] Ground and capsule-test ejection candidates, expanding to a nearby validated road/sidewalk fallback when all immediate offsets are blocked. Bypass voluntary speed restriction on wreck. If no safe point exists, keep a protected recovery state and retry; never force a spawn through geometry. Use the same appearance and sprint setup as voluntary exit.
- [ ] Preserve the existing five-second reset cooldown and health-preserving reset. Gate reset on phase/functioning state and reject unsafe destinations. Gate both local and remote explosion/automatic-repair presentation for chase.
- [ ] Land four real contact rams using player controls, scrape through cooldown, claim/re-enter reserve, then land four more. Both screens must show 100→75→50→25→wreck, character 50→0, one result, preserved relays, and no automatic repair. Repeat blocked/high-speed ejection and wrong-owner entry.

### Task 5: Validate the shared city and useful pursuit layout

**Files:** Modify `Source/Citix/Chase/CitixChaseGameMode.h/.cpp`, `CitixChaseGameState.h/.cpp`, `Source/Citix/Player/CitixDrivingPlayerController.cpp`; reuse `Source/Citix/Sandbox/CitixRouteHelper.h`, `Source/Citix/City/CitixRoadNetwork.h`; Test `Source/Citix/Chase/CitixChaseRulesTest.cpp`.

**Interfaces:** Consume `FCitixRouteHelper::FindRouteNodes(const FCitixRoadNetwork&, const FVector&, const FVector&, TArray<int32>&) -> bool`, graph edge lengths/adjacency, and generator resolved seed. Add GameMode `bool BuildChaseLayout(const FCitixRoadNetwork& Roads)` and `bool ValidateChaseLayout(const FCitixRoadNetwork& Roads, FString& OutReason) const`. Replicate Chase GameState `int32 CitySeed`, `uint32 CityConfigHash`, relay/exit/parking location arrays and completed flags. Compare local city identity before allowing ready.

- [ ] Add `CitixChase.Loop.Layout`: disconnected/blocked candidates rejected; both exits connected and separated; relay approaches usable; all eligible pursuit segments have two reserve choices with safe walking paths, nearest within 10000 cm; same layout and role spawn transforms survive round swap. Different city identity prevents ready.
- [ ] Replace coordinate-only snapping with clearance-checked candidates on the connected network. Reuse routing helper, extending it only if required for drivable-edge filtering; include endpoint offsets and same-edge cases. Keep walking validation distinct from driving reachability; ground/obstacle-check sidewalks and crossings.
- [ ] Measure spawn route time with the shared vehicle under ordinary driving, target 15–25 seconds, and check alternate routes. If full-city reserve coverage cannot be made safe, report failed layout validation and adjust parking coverage; do not silently shrink the city into a different mode.
- [ ] Store layout and role spawns once per match; replicate identity before local city setup/ready. Extend existing mismatch check to Chase GameState rather than inheriting sandbox state. Show only the runner's nearest two reserve markers after wreck and retire them after claim.
- [ ] Run layout tests on the default generated city and two additional seeds; inspect approaches/walking routes in rendered play. Record measured travel/coverage, not only Euclidean distances.

### Task 6: Finish reveal, feedback, and restrained presentation

**Files:** Modify `Source/Citix/Chase/CitixChaseGameMode.cpp`, `CitixChaseGameState.h/.cpp`, `Source/Citix/Player/CitixDrivingHUD.h/.cpp`, `CitixDrivingPlayerController.h/.cpp`, `Source/Citix/Vehicle/CitixVehiclePawn.cpp`; author `Content/Citix/Materials/M_ChaseOutline.uasset` only for the requested outline; reuse existing lighting/fog/audio. Test rendered host/guest and rule timing checks.

**Interfaces:** Replicate `APawn* RunnerPawn` and `float NextRevealServerTime` in Chase GameState; update on possession/wreck/reserve claim. Add controller `void UpdateRunnerRevealVisuals()` using GameState server time and the local role. Keep existing 3/2/25 timing; use the current pawn reference, not server-only PlayerState owner or abandoned car iteration.

- [ ] Add `CitixChase.Loop.RevealTiming`: initial reveal lasts three seconds, later two every 25, ends outside pursuit; current runner pawn changes through exit/entry. Test on-foot target and chaser as well as car-to-car.
- [ ] Add local chaser-only outline presentation for the current runner and a runner countdown/warning. Keep collision/visibility unchanged; clear reveal effects on result/disconnect. Remove the competing tracker ring once outline communicates location reliably.
- [ ] Show distinct hit, wreck/protection, reserve claimed, relay complete/interrupted, exits unlocked, and victory/defeat/draw feedback. The HUD must always explain the current win condition and show hold/reveal feedback without overlap.
- [ ] Apply restrained fog/lighting and existing sound to support pursuit without obscuring roads. Check daylight/night visibility, car/foot switching, remote particles, and performance on the user's machine; use no new atmosphere subsystem.
- [ ] Capture lobby, chase, relay hold, reveal, wreck, capture, round result, and match result screenshots on both roles. No invented driving/fairness scores from source alone.

### Task 7: Provide host/join and a clear lobby without matchmaking services

**Files:** Create `Source/Citix/Chase/CitixChaseLobbyWidget.h/.cpp`; Modify `Source/Citix/Player/CitixDrivingPlayerController.h/.cpp`, `Config/DefaultEngine.ini`, `Docs/PLAY_CITIX_CHASE.md`, `Docs/CONTROLS.md`, `PROJECT_GOAL.md`, `CURRENT_STATE.md`, `README.md`. Reuse installed UMG/Slate; no new dependency or online service.

**Interfaces:** Add controller `void ShowChaseLobby()`, `void HostChaseMatch()`, `void JoinChaseMatch(const FString& Address)`. Widget consumes Task 2 individual ready state and Task 5 identity validation; routes ready through existing `ServerChaseInteract()`. Native travel hosts `/Engine/Maps/Templates/Template_Default?listen` and joins a validated address using `ClientTravel`.

- [ ] Reproduce cold launch: no discoverable connection screen. Test blank/invalid address, unreachable host, full server, and incompatible city identity; each must show a recoverable message rather than indefinite free driving.
- [ ] Build one minimal widget with Host, Join/address, connection status, two player slots, ready actions, and current instructions. Handle network/travel failures locally; remove gameplay input while address entry is focused and restore it after closing.
- [ ] Disable splitscreen for this two-PC mode. Keep the generated-city map bootstrap; an authored map is unnecessary for the basic connection flow. Preserve normal F ready in connected Waiting and final results.
- [ ] Rewrite remake-facing goal/current-state/controls around the chase loop and preserve historical notes as clearly labeled history. Explain one-player Waiting and the temporary local two-instance testing option. Remove GTA instructions from current player docs.
- [ ] Launch host/join on two PCs without console commands; reject a third connection; verify failed join can retry and survivor can return to lobby after disconnect.

### Task 8: Establish evidence for the complete loop and package

**Files:** Modify `Source/Citix/Chase/CitixChaseRulesTest.cpp`, GameMode `ChaseTestTick`, `Tools/chase_mp_test.ps1`; Create `Docs/CHASE_ACCEPTANCE_RESULTS.md`.

**Interfaces:** Consume Tasks 1–7. Keep `-CitixChaseTest` and `-CitixChaseTestCapture` as state-machine smoke tests, with exact assertions on role, target, health, reserve, score, and phase. Separate results for synthetic transitions, rendered ordinary-input tests, real two-PC tests, and package launch.

- [ ] Fix smoke script to retain its own `Start-Process -PassThru` handles, await/check connection readiness and exit status, and stop only those processes in `finally`. It must not terminate another Citix project or session. Give explicit nonzero failure exit on missing assertions/timeouts.
- [ ] Run fresh editor/runtime builds and editor automation using the commands below. Fix failures before claiming progress. Existing logs remain historical and must not be relabeled as new verification.
- [ ] Run scripted escape/wreck and capture paths, asserting exact health 50/0, owned replacement, both roles, round two and final rematch. Negative cases must be explicit assertions, not absence of an error message.
- [ ] Complete the normal-control acceptance matrix on two PCs: both escape zones; timeout; four real rams; sustained contact; blocked/high-speed ejection; capture release/range/sight/protection; reserve exclusivity; relay persistence; reset; round swap; win/draw/rematch; disconnect in each phase; third connection; GTA input rejection.
- [ ] Repeat driving/hit/result/reveal with approximately 100 ms one-way packet lag and 2% packet loss, then restore defaults. Record simulation settings and disagreement/failure cases; compare both client logs and screenshots.
- [ ] Cook/package fresh Shipping output to `PackageShipping`, launch its actual launcher on both PCs, and repeat one full match. Preserve a baseline hash manifest of the original project's source/config/content before implementation and compare it after; source root must remain unchanged.

### Task 9: Review the experience and record initial balance

**Files:** Create `Docs/CHASE_PLAYTEST_REVIEW.md`; Modify only existing tuning/presentation files warranted by observed issues; refresh `Docs/CITIXCHASE_PROGRESS_HANDOUT.md` after verified changes.

- [ ] Play at least ten paired matches with comparable players. Record role/player, round win reason, first encounter time, recovery success, and final score. Treat results as initial evidence, not proof of fairness.
- [ ] Review the complete game as a new player. Score driving feel, chase tension, fairness, clarity, and visual coherence honestly from 1–10. Identify the three largest weaknesses and make the single most valuable change first.
- [ ] Improve within scope, at most three major cycles. Tune damage/reveal/objective/round values before abilities; retain identical driving. Repeat affected acceptance checks and compare before/after.
- [ ] Stop when high-impact problems are resolved and remaining issues are minor/subjective or further changes risk overengineering. Report remaining limitations explicitly; incomplete balance or unavailable two-PC testing stays outstanding.

## Verification commands for execution

These are planned commands, not checks performed during this audit. Close/restart the editor as needed for a normal build; preserve the working Live Coding target configuration unless fresh evidence requires changing it.

```powershell
& 'E:\UE_5.8\Engine\Build\BatchFiles\Build.bat' CitixEditor Win64 Development '-Project=E:\UnrealProjects\CitixChase\Citix.uproject' -WaitMutex
& 'E:\UE_5.8\Engine\Build\BatchFiles\Build.bat' Citix Win64 Development '-Project=E:\UnrealProjects\CitixChase\Citix.uproject' -WaitMutex
& 'E:\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'E:\UnrealProjects\CitixChase\Citix.uproject' -unattended -NullRHI '-ExecCmds=Automation RunTests CitixChase' '-TestExit=Automation Test Queue Empty' '-ReportExportPath=E:\UnrealProjects\CitixChase\Saved\ChaseAutomation' -log
& 'E:\UE_5.8\Engine\Build\BatchFiles\RunUAT.bat' BuildCookRun '-project=E:\UnrealProjects\CitixChase\Citix.uproject' -noP4 -platform=Win64 -clientconfig=Shipping -build -cook -stage -pak -iostore -archive '-archivedirectory=E:\UnrealProjects\CitixChase\PackageShipping'
```

Expected: build exit 0; automation report contains all planned `CitixChase` cases with zero failures; UAT exit 0 and current cooked artifacts. A queue-empty exit alone does not prove any test ran. The gameplay acceptance matrix still requires rendered normal-control play.

## Plan review and handoff

This plan covers the approved loop and preserves the original foundation. The principal refinement from the earlier progress account is to separate written rules and scripted transitions from player-facing completion. Tasks 1–4 supply the first playable milestone; the remaining tasks close the specified placement, presentation, connection, packaging, and balance requirements.

**Current scope:** audit and planning only. No implementation task above has been marked complete by this document. Do not claim that writing this plan repairs gameplay or verifies a match.
