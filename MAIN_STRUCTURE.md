# CitixChase — Main Structure

Two-player city pursuit built as a separate Unreal Engine 5.8 project at
`E:\UnrealProjects\CitixChase`, reusing the city generator, driving controls,
vehicle physics, traffic and on-foot movement from `E:\Citix\Citix`. The original
project is preserved.

This document is the master structure: the design contract in parts 1–4, and the
mapping of that contract onto the codebase in part 5.

## 1. Summary

Create a separate Unreal Engine 5.8 project at `E:\UnrealProjects\CitixChase`,
reusing the city generator, driving controls, vehicle physics, traffic, and on-foot
movement from `E:\Citix\Citix`. Preserve the original project.

Build a **1v1 escape game**: one runner, one chaser, two PCs using host/join.
Interpret the Identity V reference as an eerie, stylized atmosphere and asymmetric
pursuit; retain readable roads and responsive driving.

## 2. Complete round loop

**Ready → countdown → objectives → pursuit → possible wreck/recovery → escape or
capture → results → swap roles.**

- Start a five-minute round with both players in cars at random, validated road
  positions. Choose separate positions with roughly 15–25 seconds of driving
  between them and multiple usable routes.
- Place five relay objectives around the city. The runner activates any three by
  getting out and holding interact for three uninterrupted seconds. Completed
  relays remain completed after a wreck.
- Completing three relays opens two separated escape locations. The runner wins by
  driving into either escape zone.
- The chaser wins through capture, the runner's second destroyed car, or time
  expiring. The HUD always explains the current win condition.
- Both players can voluntarily exit and re-enter their own functioning cars.
  Exiting never restores vehicle health.
- Each match contains two rounds with roles swapped, using the same city, objective
  layout, and role-specific spawn positions. Most round wins decides the match; a
  tied match remains a draw. Results offer a ready-for-rematch action.

## 3. Chase rules and balance

| Mechanic | Initial rule | Purpose |
|---|---|---|
| Location outline | Reveal the runner for three seconds at the start, then two seconds every 25 seconds, including on foot. Show a reveal countdown to the runner. | Prevent endless searching while allowing concealment. |
| Car damage | Runner car starts at 100 health; each qualifying chaser ram removes 25. | Four readable hits before a wreck. |
| Valid ram | Server confirms chaser-to-runner contact with closing speed of at least 15 km/h. Require separation and a 1.5-second cooldown before another hit. | Prevent scraping or sustained contact from draining health instantly. |
| Other collisions | Buildings and traffic cause physical disruption without health damage. The chaser car cannot be destroyed in v1. | Keep damage attributable to the chase. |
| First wreck | Disable the car, force safe ejection, and subtract 50 points from 100 character health. Give three seconds of capture protection. | Create a recoverable turning point. |
| Replacement | One fresh car per runner per round, available at marked parked-car locations. Highlight the nearest two after a wreck; claiming one consumes the reserve. Character health remains 50. | Keep driving central without unlimited health resets. |
| Capture | Chaser must exit and hold interact for two seconds within 2.5 metres with unobstructed sight. Leaving range interrupts capture. | Give the on-foot phase a distinct conclusion. |
| On-foot pursuit | Chaser sprint speed is 10% higher; retain existing movement controls. | Make pursuit viable while the runner seeks cover or a replacement. |
| Second wreck | Character health reaches zero and the round ends. | Provide a clear final limit. |

Keep initial driving performance identical for both roles. Tune damage, reveal
frequency, objective duration, and round time before adding special abilities.

Validate objective, escape, and replacement placement against the connected road
network. Provide multiple approaches, and ensure eligible pursuit areas have
reachable replacement parking within approximately 100 metres along safe walking
routes.

## 4. Implementation approach

1. **Establish the reusable foundation.** Copy required source, configuration, and
   assets into the remake. Preserve steering, braking, drifting, boost, cameras,
   and engine feedback. Remove dependencies on the GTA sandbox from reused
   controllers, vehicles, and HUD.
2. **Replace the game layer.** Use a dedicated chase GameMode for authoritative
   round transitions, GameState for replicated timer/objective/results information,
   and PlayerState for role, character health, and reserve usage. Vehicle health
   stays with the vehicle.
3. **Implement interactions on the server.** Validate rams, relay activation,
   capture, vehicle ownership, replacement claims, and escape. Clients request
   actions and display replicated results.
4. **Remove GTA gameplay from the remake.** Remove weapons, shops, money, jobs,
   wanted stars, police, pedestrians, sandbox progression, and their inputs/UI.
   Keep traffic. Remove automatic car repair, destructive explosions, and ordinary
   death/respawn behavior.
5. **Close recovery loopholes.** Vehicle reset only rights/repositions a stuck car,
   preserves health, and has a cooldown. Forced ejection bypasses the ordinary
   speed restriction and finds clear ground nearby. Destroyed cars cannot be
   re-entered.
6. **Finish the experience.** Provide host/join, two-player ready state, countdown,
   role instructions, objective/reveal indicators, separate car and character
   health, capture progress, and clear results. Disconnect cancels the current round
   without awarding a win and returns the remaining player to the lobby.

Use restrained lighting, fog, silhouettes, and sound to establish the atmosphere.
Prioritize legible chase routes and immediate hit feedback. Defer inventories,
upgrades, additional modes, matchmaking services, and power-ups.

## 5. Verification and refinement

- Build and package the remake; verify the original project remains unchanged.
- Run a host and remote client through complete rounds with each player in both
  roles.
- Verify four separate rams cause exactly one wreck and one 50-point
  character-health loss; repeated contact cannot duplicate damage.
- Verify protected ejection, interrupted capture, replacement ownership, preserved
  objective progress, and second-wreck defeat.
- Test both exits, timeout, disconnect, rematch, stuck-car reset, and
  blocked/high-speed ejection.
- Test remote driving and hit feedback under simulated latency; confirm both clients
  agree on damage and results.
- Play at least ten paired matches with comparable players. Record win reasons, time
  to first encounter, recovery success, and role win rates. Treat these as initial
  evidence, then adjust the existing tuning values.
- Apply the requested self-review loop, up to three passes: score driving feel,
  chase tension, fairness, clarity, and visual coherence; fix the largest weaknesses
  before adding features.

All numerical values above are prototype defaults, not claims of proven balance.

---

# Part 5 — Structure in the codebase

## 5.1 Where the layers live

| Layer | Location | Notes |
|---|---|---|
| Chase game layer | `Source/Citix/Chase/` | The remake's game rules and authority |
| Reused city builder | `Source/Citix/City/` | Plan → build → chunks (unchanged from Citix) |
| Reused vehicle | `Source/Citix/Vehicle/` | Physics car, cameras, fleet, audio, smoke |
| Reused on-foot | `Source/Citix/Character/` | Third-person pawn used for relays/capture |
| Reused traffic | `Source/Citix/Traffic/` | Presentation-only cars, kept for the chase world |
| Reused world | `Source/Citix/World/` | Time of day, weather, street lighting |
| Reused bootstrap | `Source/Citix/Player/` | Driving GameMode, controller, HUD |
| Removed from play | `Source/Citix/Sandbox/` | Weapons/shops/police/jobs — compiled, not part of the chase loop |

## 5.2 Chase classes

| File | Type | Responsibility |
|---|---|---|
| `Chase/CitixChaseGameMode.h/.cpp` | `ACitixChaseGameMode : AGameModeBase` | Authoritative round state machine: lobby/ready, countdown, pursuit, results, role swap, rematch. Validates relays, rams, capture, replacements, exits, timeout, and disconnect. |
| `Chase/CitixChaseGameState.h/.cpp` | `ACitixChaseGameState : AGameStateBase` | Replicated timer, phase, relay progress, reveal state, interception state, exit unlock, status text. |
| `Chase/CitixChasePlayerState.h/.cpp` | `ACitixChasePlayerState : APlayerState` | Per-player role (`ECitixChaseRole`), character health, replacement reserve, ram count, rounds won. |
| `Chase/CitixChaseRules.h` | `FCitixChaseRules` | Pure rules constants/helpers shared by the mode and the test: 100 car health, 25 per ram, 15 km/h minimum closing speed, 1.5 s separation, 3 relays to unlock exits. |
| `Chase/CitixChaseRulesTest.cpp` | Automation test | Editor-only (`WITH_EDITOR && !IS_MONOLITHIC`) unit test of the rules. |

## 5.3 Round state machine

```
Waiting (lobby / ready)
   │  both players ready (F)
   v
Countdown ──► Pursuit ──┬─► runner completes 3 relays ─► exits unlock
   ▲                    ├─► chaser rams runner car x4 ─► wreck (eject, -50 hp, protection, reserve)
   │                    ├─► replacement claimed (health stays 50) ─► pursuit
   │                    ├─► capture (exit + 2 s interact in range/LOS) ─► chaser wins
   │                    ├─► second wreck (character health 0) ─► chaser wins
   │                    └─► time expires ─► chaser wins
   │
   └──────────── Results (round winner) ──► swap roles ──► second round ──► match result (rematch ready)
```

Replicated surface used by the HUD: phase, phase seconds remaining, completed
relays, exits-unlocked, status text, runner revealed + reveal countdown,
interaction active + interaction countdown, per-player role and character health.

## 5.4 Reuse boundaries

- **Kept and shared:** procedural city, vehicle physics/steering/braking/drift/boost,
  chase + hood cameras, engine audio and drift smoke, on-foot movement, traffic
  population, synchronized time of day, replicated patrol of the world.
- **Retained but not part of the chase loop:** the sandbox director and its systems
  (they still compile; the chase GameMode does not drive them).
- **Disabled for tone:** pedestrians.
- **Preservation boundary:** all remake work stays under
  `E:\UnrealProjects\CitixChase`; `E:\Citix\Citix` is never modified.

## 5.5 Build and run

| Purpose | Command |
|---|---|
| Build editor | `Engine\Build\BatchFiles\Build.bat CitixEditor Win64 Development -Project="…\Citix.uproject" -WaitMutex` |
| Build standalone game | `Engine\Build\BatchFiles\Build.bat Citix Win64 Development -Project="…\Citix.uproject" -WaitMutex` |
| Build headless server | `Engine\Build\BatchFiles\Build.bat CitixServer Win64 Development -Project="…\Citix.uproject" -WaitMutex` |
| Run standalone | `Binaries\Win64\Citix.exe` |
| Author emissive/water materials | `UnrealEditor-Cmd.exe …\Citix.uproject -run=CitixMaterialSetup -unattended -nosplash -stdout` |

**Build-flag invariant (important):** `bWithLiveCoding` is pinned explicitly in
`Citix.Target.cs` and `CitixServer.Target.cs`. It controls `WITH_LIVE_CODING`, which
controls `WITH_RELOAD`, which changes the layout of the UHT compiled-in registration
structs. The engine's precompiled game objects are built with live coding enabled;
if the project module disagrees, CoreUObject walks the registration arrays with the
wrong stride and the game crashes during static init. Keep the project module's
`WITH_LIVE_CODING` identical to the engine's.

---

# Part 6 — Current status

The full round loop is implemented, playable host/join, and verified headlessly by an
in-engine test (`-CitixChaseTest` on the host, plus `-CitixChaseTestCapture` for the
capture path). The test drives both drivers through a whole match and logs `PASS`/`FAIL`
for each rule.

## Verified loop (host + one guest)

| Step | Result |
|---|---|
| Both press ready (F in car / on foot) | `Both drivers ready - starting round 1` |
| Countdown → pursuit | `Pursuit begins (round 1)` |
| Three relays on foot | `Relay complete: 1/3`, `2/3`, `3/3 - exits unlocked` |
| Drive into an exit | `Runner wins - Runner escaped` |
| Second round, roles swapped | `Pursuit begins (round 2)`, roles reversed |
| Four accepted rams | `Ram 1/4..4/4`, `Runner car wrecked - health now 50`, two replacements spawn |
| Claim a replacement | runner enters a replacement car, health stays 50 |
| Four more rams | `Chaser wins - Runner car destroyed` (second wreck) |
| Capture (alt. path) | chaser ejects, holds interact → `Chaser wins - Runner captured` |
| Match + rematch | `Match complete`, both ready again → `rematch restarted` |
| Both clients agree | client log mirrors phase/timer/relays/hp/hits/role/rounds every 2 s |

## Fixes made to close the loop

- **Player starts.** The chase GameMode now spawns a lobby `APlayerStart` per driver and
  overrides `ChoosePlayerStart` with per-controller claims. Without this, clients were
  never possessed (`FindPlayerStart` failed) and their car sat frozen as a simulated proxy.
- **Client interaction.** Relay/capture/ready were gated on `GetAuthGameMode<ACitixChaseGameMode>()`,
  which is null on clients, so guests could never interact. All gates now use
  `IsChaseMode()` (a replicated `ACitixChaseGameState` check) and route through the
  existing Server RPCs.
- **Ram separation rule.** "Separated since the last hit" was evaluated at the moment of
  contact (always inside 650 cm), which rejected every repeat hit. It is now latched:
  a hit clears the flag and the pair must physically move apart again before the next
  hit counts, alongside the 1.5 s cooldown.
- **Replacement cars.** They spawned flagged occupied, so `FindNearestVehicle` skipped
  them and a wrecked runner could never claim one. They now spawn free.
- **Round telemetry.** Every round transition, relay, ram, wreck and result is logged
  (`[CitixChase] ...`) so a match can be read back from the log.

## Car display (stutter)

Every non-authority car copy is driven by **buffered snapshot interpolation**, not
per-snapshot dead reckoning:

- Arriving movement snapshots are pushed into a small time-stamped buffer (with an
  adaptive mean interval). Teleports clear the buffer.
- The render pose is sampled at `now - delay` and blended between the two snapshots that
  bracket that time (position lerp, quaternion slerp rotation), with clamped velocity
  extrapolation only when the buffer is starved.
- The owner's copy uses a short delay (responsiveness); remote copies use a longer one
  (smoothness).

Measured with `-CitixSmoothLog` on the guest's own car while driving, against the old
dead-reckoning path (`-CitixLegacyInterp`): max per-frame commanded step fell from
**19–46 cm to 5–15 cm**, and motion reversals from **~120/3900 frames to ~35/3900**.

### Turning and replicated rotation

`FRepMovement::RotationQuantizationLevel` defaults to `ByteComponents` — one byte per
axis, i.e. **~1.4° per step**. That is fine for forward motion but reads as steppy
turning on every client copy. `ACitixVehiclePawn` now sets, once per instance:

- `RotationQuantizationLevel = ShortComponents` (~0.006°),
- `LocationQuantizationLevel` and `VelocityQuantizationLevel = RoundTwoDecimals`.

Measured on a driving guest's own car, the commanded yaw step dropped from the ~1.4°
quantisation floor to **mean 0.01–0.04°, max ≤0.27°**.

### Wheel and particle presentation on clients

Clients do not simulate the car, so `UCitixVehicleMovementComponent` has no state there.
Both pooled particle systems and the cosmetic wheels now read the replicated state:

- `FCitixVehicleNetState` carries `bHandbrake` and `LateralSlip`; the drift-smoke
  component falls back to it on non-authority copies (and emits from a rear-axle offset
  instead of the missing wheel positions).
- The boost-trail component uses the pawn's `IsDisplayBoosting()` (movement on the
  authority, replicated `NetState.bBoosting` on clients).
- Remote wheel spin and steer are eased (`RemoteSpeedKmh`, `RemoteSteerDeg`) instead of
  snapping to the 15 Hz snapshot.

Verified with `-CitixNetDrift` (holds throttle + handbrake + boost): the guest's own car
reports `smoke=52 flames=38 ... handbrake=1` where it previously reported zero.

## Test switches

| Switch | Behaviour |
|---|---|
| `-CitixChaseTest` | Host runs the scripted full-loop test (ready → relays → exit → round 2 → wreck → replacement → second wreck → match → rematch) |
| `-CitixChaseTestCapture` | Round 2 resolves by capture instead of the second wreck |
| `-CitixChaseReady` | Each instance auto-readies in the lobby/rematch |
| `-CitixSmoothLog` | Logs per-2 s movement smoothness statistics (position + yaw) |
| `-CitixNetDrift` | Holds throttle + handbrake + boost on every instance (particle test) |
| `-CitixLegacyInterp` | Uses the old dead-reckoning path (A/B measurement) |

`Tools/chase_mp_test.ps1` launches a listen host and one (or two) guests with these
switches and collects `Saved/Logs/ChaseMP_*` logs.

## Still to verify

- 5-minute timeout expiry (logic is a plain timer decrement).
- Disconnect mid-round (Logout cancels the round and frees the start claim).
- Two PCs on separate machines (the automated pass is loopback on one machine).

