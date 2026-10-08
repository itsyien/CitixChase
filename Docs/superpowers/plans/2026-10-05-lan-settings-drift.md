# LAN rooms, performance sliders and drift implementation plan

> **For agentic workers:** Use superpowers:executing-plans for orchestration and superpowers:dispatching-parallel-agents for the independent settings/drift changes. The user explicitly requested implementation; continue without another design gate.

**Goal:** Ship approachable LAN multiplayer, saved render-scale/FPS sliders and responsive tight-corner drifting.
**Architecture:** A GameInstance subsystem wraps the bundled NULL Online Subsystem session APIs. UMG consumes connection state rather than issuing travel directly. Independent saved graphics controls and grounded drift assistance extend existing systems.
**Tech stack:** Unreal Engine 5.8 C++, UMG, OnlineSubsystemNull, Chaos vehicle physics.
**Spec:** `Docs/superpowers/specs/2026-10-05-settings-rooms-drift-design.md` (approved scope: LAN only).

## Global constraints

- Preserve two players, roles, city verification, Ready, rematch, gameplay rules, abilities and city generation.
- Resolution: 30%–110%; FPS detents: 30, 60, 120, 144, 240, Unlimited.
- Preserve manual IP under collapsed Advanced; no EOS integration in this delivery.
- Preserve the existing packaged builds. This directory is not a Git checkout: keep file backups instead of inventing a branch/worktree.
- Two local processes do not prove two-computer LAN joining. Report that validation limit explicitly unless a second device becomes accessible.

## Review focus

- Switching presets or travelling must not erase a saved FPS cap or manual render scale.
- Cancelled searches/connections and late delegates must not unexpectedly travel or restore stale room rows.
- A room filling after discovery must fail clearly at admission; incompatible builds must show readable feedback.
- Host departure must return the guest to a usable lobby with retry, not leave a stale connected screen.
- Drift must not create speed or apply ground assistance in mid-air; releasing steering must stop continued spin.

## Task 1 — Performance controls

Files: `Core/CitixGraphicsSettings.{h,cpp}`, `Chase/CitixSettingsWidget.{h,cpp}`, new `Core/CitixPerformanceSettingsTest.cpp`.

- [x] Add failing behavior coverage for keeping cap/manual scale when switching a preset; record red before implementation.
- [x] Add saved independent manual resolution percentage and FPS detent. Restore recommended resolution only through Use Automatic.
- [x] Add real UMG sliders with live labels, native marker, six FPS snaps, focus support and saves at interaction completion. Verify the actual 110% runtime percentage beyond the standard scalability clamp.
- [x] Build/test persistence and runtime values; capture settings screenshots at 720p/1080p.

## Task 2 — LAN connection manager and lobby

Files: new `Network/CitixSessionSubsystem.{h,cpp}`, `Network/CitixSessionTest.cpp`, `Chase/CitixChaseLobbyWidget.{h,cpp}`, controller connection methods, GameMode admission/membership hooks, module/plugin/config entries.

Interfaces: subsystem exposes `HostRoom(FString)`, `FindRooms()`, `JoinRoom(int32)`, `JoinAddress(FString)`, `Cancel()`, `Retry()`, `LeaveRoom()`, `NotifyWorldReady()` and `UpdateOccupancy()`. Public read-only state contains operation, message, room name and compatible room results. `ProtocolVersion` metadata gates discovery/admission.

- [x] Record a failing test for the missing GameInstance connection manager, then test normalization/admission and cancellation against the real implementation.
- [x] Enable NULL session APIs. Create two-slot advertised sessions with name/version/city-game metadata, discover with LAN query and resolve addresses through JoinSession.
- [x] Manage delegates, operation timeouts, cancellation and session cleanup across world travel. Route network/travel failures to the subsystem. Track authoritative PlayerArray membership and occupancy once per second without changing round logic.
- [x] Replace direct-address opening UI with Host Game/Find Nearby Games, friendly name input, scrollable room rows, Searching/Connecting states, Retry/Cancel, Leave and collapsed Advanced IP. Keep Ready in the connected view.
- [x] Add a real UI/connection probe, exercise discover/join/ready plus failures/cancel/full/version handling; prove logs and screenshot results.

## Task 3 — Drift responsiveness

Files: `Vehicle/CitixVehicleMovementComponent.{h,cpp}`, `Vehicle/CitixVehiclePawn.cpp`, new `Vehicle/CitixDriftResponseTest.cpp`, optional isolated runtime probe.

- [x] Capture a reproducible baseline/failing behavior check for the turn response, using actual physics rather than only property-value assertions.
- [x] Add grounded, speed-bounded yaw assistance and planar momentum/alignment damping while drifting with active steering. Preserve role-specific normal steering, speed caps, brakes, boost, vertical motion and existing ice freeze rules.
- [x] Compare left/right turn rotation and old-forward distance against baseline; check release/countersteer, no speed gain, airborne exclusion and frame-rate behavior.

## Task 4 — Integration and product review

- [x] Run full Citix automation and packaged host/find/join without an IP; exercise the existing round/rematch regression.
- [x] Build separately under `PackageLAN`; inspect real lobby, room list and settings visuals, and compare actual drift physics. Run fresh independent code review and fix high-impact issues.
- [x] Perform user-required self-review, score five intended qualities honestly and make up to three improvement passes.
- [x] Preserve evidence and delivery notes; report exact validation and remaining two-device limitation.

## Execution ledger

- Ruling: user narrowed multiplayer to LAN and said “please implement the plan”; no further design confirmation. EOS is deferred.
- Ruling: current folder has no Git repository; retain scoped backups under Saved rather than create a worktree.
- Ruling: settings and drift touch separate files and may be delegated under the dispatching-parallel-agents skill; the parent owns session integration, builds and packaged tests.

- Final evidence: all 12 automation tests pass in LANSettingsDrift-FinalVerified.log; packaged host/find/join/Ready and both rendered graphics probes pass with isolated profiles. Existing two-round/rematch regression passes in ChaseMP_lan_round_regression_Host.log. Two physical computers and subjective street-corner driving remain external validation limits.
