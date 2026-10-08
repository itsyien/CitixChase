# Networked Chase Polish Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the joined driver responsive, synchronize the city, and add clear asymmetric pursuit feedback and runner escape opportunities.

**Architecture:** Keep the server authoritative for game rules, vehicle physics snapshots, clock state, and map interactions. The owning driver predicts local input and reconciles to snapshots; clients render replicated cosmetic and chase presentation state. Existing chase GameMode, GameState, PlayerState, vehicle pawn, time-of-day, and HUD remain the integration points.

**Tech Stack:** Unreal Engine 5.8, C++, UMG/Canvas HUD, replicated actor state, existing Citix vehicle and weapon systems.

**Spec:** `Docs/superpowers/specs/2026-10-03-networked-chase-polish-design.md`

## Global Constraints

- Preserve server authority for rams, health, objectives, capture, and round state.
- Do not add dependencies or a second weapon/vehicle system.
- The city cycle is exactly 180 seconds; every reveal lasts 10 seconds.
- Chaser top speed is 130 percent; runner engine force is 250 percent of the chaser baseline.
- Breakaway stations require a three-second on-foot hold, are single-use per round, and last eight seconds.

## Review Focus

- Guest input under 150 ms simulated lag should remain locally responsive while the server stays authoritative.
- A late-joining client must render the current server time without a visible day/night jump loop.
- Repeated collision contact must produce one accepted ram per cooldown and one feedback burst per accepted ram.
- Runner-only breakaway use must reject chaser, vehicle, out-of-range, and consumed-station attempts.
- Chaser pistol input must be rejected while driving or controlled by the runner.
- The HUD must keep one focal instruction and hide secondary information outside its relevant state.

---

### Task 1: Replicated vehicle presentation and guest prediction

**Files:**
- Modify: `Source/Citix/Vehicle/CitixVehiclePawn.h`, `Source/Citix/Vehicle/CitixVehiclePawn.cpp`
- Modify: `Source/Citix/Vehicle/CitixDriftSmokeComponent.cpp`, `Source/Citix/Vehicle/CitixBoostTrailComponent.cpp`
- Test: `Source/Citix/Chase/CitixChaseRulesTest.cpp`

**Interfaces:**
- Produces replicated brake/boost presentation fields and owner prediction/reconciliation used by vehicle cosmetics.

- [ ] Write failing rule checks for replicated boost/brake cosmetic state and owner reconciliation tolerance.
- [ ] Run the automation test and verify the new checks fail.
- [ ] Add owner-predicted input simulation with smooth authoritative reconciliation; drive smoke/flames from replicated state on non-owning clients.
- [ ] Run Development build and two-process 150 ms lag regression; verify no city mismatch or rule failure.

### Task 2: Authoritative city clock

**Files:**
- Modify: `Source/Citix/Chase/CitixChaseGameState.h`, `Source/Citix/Chase/CitixChaseGameState.cpp`
- Modify: `Source/Citix/Chase/CitixChaseGameMode.cpp`
- Modify: `Source/Citix/World/CitixTimeOfDay.h`, `Source/Citix/World/CitixTimeOfDay.cpp`
- Test: `Source/Citix/Chase/CitixChaseRulesTest.cpp`

**Interfaces:**
- Produces `ReplicatedDayFraction` in GameState and `SetAuthoritativeDayFraction(float)` on local time-of-day actors.

- [ ] Write failing checks for a 180-second cycle and normalized time wrapping.
- [ ] Run the checks and verify failure.
- [ ] Advance and replicate the server clock; render the replicated fraction locally without independently advancing a client clock.
- [ ] Run a two-process startup/late-join check and verify both clients log the same clock fraction.

### Task 3: Role tuning, reveal, HUD hierarchy, and accepted-hit feedback

**Files:**
- Modify: `Source/Citix/Chase/CitixChaseGameMode.cpp`, `Source/Citix/Chase/CitixChaseGameState.h`
- Modify: `Source/Citix/Vehicle/CitixVehiclePawn.cpp`, `Source/Citix/Vehicle/CitixVehicleMovementComponent.*`
- Modify: `Source/Citix/Player/CitixDrivingHUD.cpp`
- Test: `Source/Citix/Chase/CitixChaseRulesTest.cpp`

**Interfaces:**
- Produces a replicated accepted-hit serial/location and role-specific vehicle modifiers applied by the server.

- [ ] Write failing checks for ten-second reveals, chaser damage immunity, one-quarter runner health loss, and role speed/engine multipliers.
- [ ] Run the checks and verify failure.
- [ ] Apply the role modifiers at round spawn, reverse the tracker cone only, and replicate an accepted-hit burst consumed once per client.
- [ ] Make runner health prominent and add a start banner plus compact persistent role chip; keep reveal, hold, hit, and objective instructions transient/contextual so no permanent HUD block competes with the chase view.
- [ ] Run the two-client regression and inspect that a valid hit updates both HUDs once.

### Task 4: Chaser on-foot pistol gate

**Files:**
- Modify: `Source/Citix/Player/CitixDrivingPlayerController.cpp`
- Modify: `Source/Citix/Character/CitixOnFootPawn.cpp`
- Test: `Source/Citix/Chase/CitixChaseRulesTest.cpp`

**Interfaces:**
- Consumes the replicated `ECitixChaseRole`; produces server-side weapon acceptance only for an on-foot chaser.

- [ ] Write failing checks for chaser-on-foot allowed and runner/driving denied fire requests.
- [ ] Run the checks and verify failure.
- [ ] Gate the existing pistol input and RPC at the shared server acceptance point.
- [ ] Run the rule test and a two-client capture smoke test.

### Task 5: Breakaway stations

**Files:**
- Modify: `Source/Citix/Chase/CitixChaseGameMode.h`, `Source/Citix/Chase/CitixChaseGameMode.cpp`
- Modify: `Source/Citix/Chase/CitixChaseGameState.h`, `Source/Citix/Chase/CitixChaseGameState.cpp`
- Modify: `Source/Citix/Player/CitixDrivingHUD.cpp`
- Test: `Source/Citix/Chase/CitixChaseRulesTest.cpp`

**Interfaces:**
- Produces replicated breakaway locations, consumed state, and runner advantage end time.

- [ ] Write failing checks for role/range/vehicle/consumed rejection and an eight-second granted advantage.
- [ ] Run the checks and verify failure.
- [ ] Generate two road-validated stations, reuse the server hold path for three-second activation, and apply the advantage only while active.
- [ ] Add station and remaining-duration HUD presentation.
- [ ] Run a two-process regression that proves one station consumes once and expires after eight seconds.

### Task 6: Release verification

**Files:**
- Modify: `Docs/CITIXCHASE_PROGRESS_HANDOUT.md`

**Interfaces:**
- Consumes all prior task interfaces.

- [ ] Build Development and Shipping targets.
- [ ] Package Win64 Shipping to `PackageShipping/Windows`.
- [ ] Run the full two-client regression with simulated latency.
- [ ] Perform a rendered host/guest pass covering guest driving, smoke/flames, shared clock, role UI, hit feedback, pistol gating, and breakaway stations.
- [ ] Record evidence and remaining manual balance work in the handout.
