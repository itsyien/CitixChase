# Citix — Multiplayer Migration Plan

Server-authoritative, dedicated-server model. Single-player behavior is preserved
exactly when `GetNetMode() == NM_Standalone`.

## 1. What the inspection found

- **Zero networking exists.** No `bReplicates`, no RPCs, no replicated properties,
  no GameState/PlayerState subclasses anywhere in `Source/Citix`.
- **67 player-index-0 assumptions**, concentrated in:
  - `Sandbox/CitixSandboxDirector.cpp` (~30× `GetPlayerPawn(0)` / `GetPlayerController(0)`)
  - `Player/CitixDrivingGameMode.cpp` (test hooks, all `GetFirstPlayerController()`)
  - `Traffic/CitixTrafficSystem.cpp`, `Pedestrian/*` (simulation anchored to player 0)
  - `Core/CitixVisibility.cpp` (local-player camera; invalid on dedicated servers)
- **One shared PlayerStart.** `EnsurePlayerStart` builds a single start;
  `ChoosePlayerStart` returns it for everyone → all players spawn interpenetrating.
- **Appearance is non-deterministic.** On-foot pawns roll `FMath::Rand()` when
  `AppearanceSeed == 0`; traffic/ped pools roll per spawn. Server and clients
  would build different bodies/cars for the same actor.
- **Client input mutates the world directly.** `RequestEnterVehicle` spawns actors,
  `TakeVehicle` edits the traffic pool, `FireWeapon` traces and applies damage,
  all from input handlers that would run on autonomous proxies.
- **Physics simulates wherever the actor exists.** The Chaos vehicle body would
  simulate independently on every client and diverge.
- **HUD is per-controller and mostly null-safe** (`GetOwningPawn`, guarded sandbox
  reads). It survives a missing director. Minimap centers on the owning pawn.
- **No save system exists** (session-only by design) → nothing to migrate there.
- **City geometry is deterministic from the seed** (plan numbers match exactly
  across runs) → chunks/HISMs never need replication; every instance generates
  its own identical city.

## 2. Authority model (applies to every system below)

| System | Server | Replicates | RPCs | Owner | Clients never control | Prediction |
|---|---|---|---|---|---|---|
| City geometry (chunks, roads, buildings) | generates (authoritative copy) | NOTHING (deterministic from seed) | — | — | — | n/a |
| Time of day / weather cycle | simulates | `Hours`, `RainIntensity`, paused | scrub = server console only | server | clock, weather state | no (slow state, interp not needed) |
| Traffic sim + ped sim | simulates (multi-player anchored) | pool actors (transform only) | — | server | spawn/despawn, agents | no (kinematic interp) |
| Traffic/ped appearance | rolls | type/color/style/seed (spawn bunch) | — | server | looks | n/a |
| Player spawn/possess | GameMode | automatic (possession) | enter/exit = Server RPC | server | pawn lifecycle | n/a |
| On-foot movement | validates (CharacterMovement) | built-in (moves + transform) | built-in | owning client (predicted) | — | YES (CharacterMovement, free) |
| Vehicle movement | simulates (Chaos) | transform + net state | input = Unreliable Server RPC | server | physics, position | interp only: explicit `InterpolateRemoteMovement` on every remote copy with velocity extrapolation between snapshots (the engine skips movement apply for autonomous proxies, and physics-packed snapshots only feed simulating bodies — without it all client cars freeze at spawn). Pawns update at 60 Hz, `NetState` throttled to 15 Hz, GameState publishes on change only (per-tick full pushes starved movement down to ~14 Hz) |
| Vehicle appearance | applies (takeover/swap) | type/color (spawn bunch / RepNotify) | swap = stage 2 | server | looks | n/a |
| Vehicle HUD state (hull/boost/speed) | simulates | compact state struct 10 Hz | — | server | health | no |
| Destroyed visuals | decides | destroyed flag | — | server | destruction | no |
| Sandbox progression (score/money/heat/objectives/loadout) | simulates per-driver records | owner snapshot in PlayerState (private; health/ammo/objectives replicated) | verbs = Server RPCs with acting player | server | anyone else's wallet/ammo/objective/stars | no (interp for cars, prediction on foot) |
| Weapon fire/reload/ADS | resolves per-shooter record | owner snapshot + shot-event feed (muzzle/impact/serial; remotes play local pools) | fire/reload = Server RPCs, validated origin/rate | server | ammo, cooldown, spread | no |
| Shops | validates per-buyer record | loadout snapshot (denials toast the buyer) | buy = Server RPC (proximity/funds/ownership validated) | server | funds, stock | no |
| Police pursuit | simulates per-suspect units | car transform + lightbar/disable flag; officer transform + death; per-player stars/escape/state | — | server | targets, pursuit | no |
| Photo/map verbs | local-only | nothing | — | — | time pause (standalone only) | n/a |
| Ragdoll limbs | simulates | root transform only (limbs freeze remotely — documented stage-1 limit) | — | server | bodies | no |

What breaks with 2 PlayerControllers today (must fix in stage 1):
player 0 anchoring (traffic/peds/director/visibility), single PlayerStart,
non-deterministic appearance, client-side world mutation, divergent physics,
server with no local player (all `GetFirstPlayerController` paths), time/weather
simulated per instance.

## 3. Stages

### Stage 1 — minimal loop (IMPLEMENTED + TESTED)

Server starts → two clients join on separate starts (lateral line of 8, per-controller
claims freed on logout) → each possesses a vehicle pawn → mutual visibility
(position-matched across all three logs) → on-foot movement replicates with
prediction (server tracks full walk speed) → exit/enter via Server RPCs with
ownership tracking (no joyriding) → vehicles server-simulate with input RPCs
(15 Hz) + replicated transform/state → leave/rejoin safe (timeout → Logout →
claim freed → clean rejoin). Client bootstrap builds the deterministic city +
clock + weather locally (GameMode is server-only).

Deliberate stage-1 limits: remote ragdoll limbs freeze; remote drift smoke off;
wheels freeze on remotes; photo time-pause standalone-only; G/J/E/V verbs gated;
score/money/heat stay global server-side; vehicle sync interpolates (no car
prediction — owner feels RTT lag).

Two real bugs found by testing (both fixed, both verified):
- `HasAuthority()` is TRUE for locally-spawned actors on clients. All sim gates
  now check `GetNetMode() != NM_Client` instead — otherwise every client runs a
  duplicate traffic/ped/weather/time simulation (170+ ghost cars observed).
- Remote player pawns dropped out of remote views after ~1 min. Player pawns are
  now `bAlwaysRelevant` (2-4 cars; background actors keep distance culling).
- Vehicle movement never replicated visibly: the engine skips movement apply
  for autonomous proxies, and physics-packed snapshots only feed simulating
  bodies (ours simulate server-side only), so every client car froze at spawn
  while fresh snapshots arrived ~14 Hz. Every remote copy now interpolates
  explicitly (`InterpolateRemoteMovement`: converge, snap beyond 8 m).

### Stage 2 — shared-world gameplay (SUPERSEDED by session 33 below)

Stage 2 originally mirrored one shared sim into every connection. Session 33
replaced the shared sim with independent per-driver records (`FCitixPlayerGame`
fully integrated): the bullets below describe history, not current behavior.

- ~~Server mirrors the sim into every PlayerState + the GameState at 4 Hz~~ →
  each driver's own record publishes to their own PlayerState.
- Clients keep the presentation-only director + `UpdateFromReplication` (now
  also wires loadout, reload progress and ADS into the HUD fields).
- Waypoint / delivery / activity / cancel verbs are Server RPCs (unchanged),
  now acting on the caller's record; jobs/activities/waypoints are solo-owned
  (no implicit co-op: only the initiator advances).
- ~~Combat/shop/car-swap verbs stay single-player~~ → guns (server-resolved,
  per-shooter ammo/cooldown, validated aim, echo tracers) and shops (local
  browsing, server-validated purchase) work online; car swap stays standalone.
- Listen-server host runs the same authority paths as dedicated
  (`CombatVerbAllowed` includes authority); guests are unchanged.

### Stage 3 — independent records (IMPLEMENTED + TESTED, session 33)

Per-driver money/score/heat/stars/escape/loadout/objectives/stunts, published
per owner; legacy globals mirror the focus record so HUD/tests/host view keep
working. Crimes carry instigator identity (gun/ped-kill window, ram, blast);
police units hunt named suspects with per-suspect escape/bust/death/release;
police cars/officers replicate (lightbar/disable/death/stride on remotes).
Respawn validates ground + headroom (retries, spawn protection, death closes
pursuit); officer gunfire and blasts damage players; car gunfire damages hulls.
Disconnect releases the leaver's units and clears their record (rejoin fresh).

Deliberate limits: discovery flags/cards stay shared (personal discovery is a
later item); no explicit co-op activities (solo-only, documented); no race
loop; no client clock handshake; no vehicle prediction; photo stays
attribution-only online.

### Gap audit resolution (session 33, ranks from `MULTIPLAYER_GAP_AUDIT.md`)

- Ranks 1–4 (P0): DONE and live-tested (isolation, simultaneous verbs,
  validated aim with logged rejections, per-suspect pursuits, dual-suspect
  escape). Private state replicates owner-only.
- Rank 5: DONE (car cosmetics on all instances, replicated disable, officer
  existence/pose/death/stride, real target fed to damage, per-owner status).
- Rank 6: DONE (local browsing, `ServerBuyShopRow` validates
  proximity/funds/ownership/catalogue, buyer-only denials, equip free).
- Rank 7: DONE (Y join/leave, roster, countdown, 4 ordered checkpoints, live
  positions, P1–P3 rewards once, 240 s timeout, DNF on leave/death/disconnect,
  race-again, X forfeit). No solo time-trial, no betting/leagues.
- Rank 8: DONE solo-owned jobs/waypoints/activities/contracts (no implicit
  co-op); arrival beats the clock (documented); tour text shows the actual
  total. No co-op roster UI (documented).
- Rank 9: DONE (replicated city seed verified, mismatch disconnects; duplicate
  local clock retires; clock always relevant; rain rides the clock).
- Rank 10: DONE (ground+headroom validation with retries/fallback, 2 s spawn
  protection, blast (60) and officer (10) damage, hull damage from gunfire,
  death fails job/activity, no fall damage by decision, free respawn by rule).
- Rank 11: DONE (replicated owner/occupancy, exit speed + clearance gates with
  denials, 5 s reset cooldown, inputs 15→30 Hz). No theft-of-occupied rule
  change (documented: ownership stands).
- Rank 12: measured (20 Hz snapshots LAN, ~30 cm tracking, smooth at 120 ms
  lag, 33 ms server with 4 PCs); input rate doubled; full prediction deferred
  with this baseline recorded.
- Rank 13: DONE (`CombatVerbAllowed` includes authority; host verbs/direct
  paths verified live with a guest).
- Rank 14: DONE (replicated reaction + canned remote poses + hitbox sync;
  also fixed fully-dead ped spawning).
- Rank 15: DONE (logout releases units, clears the record, frees owned cars,
  prunes feed maps; rejoin starts fresh and verified).
- Rank 16: DONE (personal POI/district sets + rewards, OwnerOnly; shared flags
  stay for map display).
- Rank 17: DONE (E works online, denial toasts, per-record cards, honest spread
  display; no general queue — bursts still collapse to latest by design).
- Rank 18: DONE (per-pawn local gun props from public `VisibleWeapon`, remote
  wheels spin/steer, engine audio by occupancy; smoke stays off by design).
- Rank 19: DONE (server-validated rewards from submitted views, local
  screenshots, replicated cooldown; possession flow kept and safe).
- Rank 20: MEASURED (4 PCs, server holds 33 ms, pool capped, distance culling
  splits fairly; no per-region reallocation — measurements don't require it).
- Rank 21: DONE (`CitixServer` target, `-CitixName=`, session-only MOTD).
  No menu/session UI (documented).
- Rank 22: session-only explicit (MOTD + docs); no persistence (documented).
- Rank 23: DONE here (vision, ownership model, statuses, acceptance).

## 5. Audit: single-player-only features (session-33 status)

Original inspection (session 32) listed items NOT in multiplayer. Session 33
closed most of them; the remainder is explicit below.

| Feature | Status now |
|---|---|
| Weapon shops + ammo economy | ONLINE: local browsing, server-validated buys, per-buyer funds/denials |
| ADS feel / scope overlay | ONLINE (local zoom + honest spread; server enforces per-shot) |
| Reload UX + spread display | ONLINE (owner snapshot drives bar + crosshair) |
| Car swap (V) | standalone only (unchanged) |
| Police pursuit visuals | ONLINE: cars/officers replicate, lightbar/disable/death sync |
| Ragdoll limbs on remotes (player cars) | frozen (documented limit, unchanged) |
| Pedestrian knockdown/death/recovery | ONLINE: compact reaction state + canned remote poses + hitbox sync |
| Drift smoke / wheels on remotes | wheels spin/steer from snapshot, engine audio by occupancy; smoke still off |
| Photo time-pause | standalone only (unchanged); scoring is server-validated online |
| Time-of-day scrub | authority-only (unchanged); clock replicates, single source enforced |
| Per-player progression | ONLINE: independent records, owner-only replication of private state |
| District entry cards on clients | ONLINE: personal feed per record |
| Kill-heat attribution per player | ONLINE: instigator window (gun/ram/blast) |
| Fall / blast damage to players | blast damages (60); falls do not (decided, documented) |
| Death penalty (score/money loss) | none (decided); death fails job/activity + closes pursuit |

### PvP combat (IMPLEMENTED + TESTED, session 32, extended 33)

Per-player health in the player state (100 max, +6/s regen after 5 s quiet).
Cars damage on-foot drivers on contact (50/hit, 1 s ram invulnerability, knock
flying); guns fire via Server RPC with server-side resolution (shared arsenal,
starter pistol on MP join, per-shot ADS flag) and a per-shooter shot-event feed
so every client plays tracers locally. Death → killfeed toast + heat, respawn
on foot in a 30–100 m ring with a tall blue sky beam (replicated, 6 s fuse).
Health bars: own (left stack, always in MP) + floating bars over other drivers.

## 4. Test commands (stage 1)

```
# dedicated server (background):
UnrealEditor-Cmd.exe Citix.uproject /Engine/Maps/Templates/Template_Default -server -nullrhi -unattended -log -CitixNetLog
# client 1 (walks: exercises exit RPC + predicted movement):
UnrealEditor-Cmd.exe Citix.uproject 127.0.0.1 -game -unattended -log -CitixNetLog -CitixWalkTest
# client 2 (idle observer):
UnrealEditor-Cmd.exe Citix.uproject 127.0.0.1 -game -unattended -log -CitixNetLog
```

PASS = server logs 2 PCs + 2 pawns; client 2 sees both pawns within 500 cm of
server positions; client 1 moves; no errors; killing + rejoining a client leaves
the server stable with correct counts. On a single laptop run both clients with
`-CitixLowSpec` (halves render cost: resolution 50, no shadows/post) — two full
cities plus a server stutter on one GPU regardless of networking.

Stage-2 verb test (client 1 also passes `-CitixVerbTest`: cycles a waypoint at
6 s, starts a delivery at 12 s):

```
# client 1 (verb driver):
UnrealEditor-Cmd.exe Citix.uproject 127.0.0.1 -game -unattended -log -CitixNetLog -CitixVerbTest
```

PASS = server logs each verb RPC (`Server: CycleWaypoint/StartDeliveryJob verb
from ...`) + `Delivery job started`; every instance's `sandbox` census line
shows the same waypoint name, `job=1` (Active), `markers>0` and matching
`score`/`discovered` counts; no errors.

Isolation test (audit rank 1; `-CitixIsoTest` on the server): A earns/buys/heats
while B's snapshot must not move; police units must all name A as suspect.

```
UnrealEditor-Cmd.exe Citix.uproject /Engine/Maps/Templates/Template_Default -server -nullrhi -unattended -log -CitixNetLog -CitixIsoTest
```

PASS = `[CitixIso]` lines show A's money/owned/heat/stars changing with B fixed
at starter values; every unit `[suspect=A]`; escape clears A's units; zero errors.

PvP test (`-CitixPvpTest` on the server, C1 `-CitixNetWalk`, C2 `-CitixNetDrive`):
staged ram + aimed shots. PASS = `took N damage (run over/shot)`, killfeed,
`respawned`, blue beam actor replicates; zero errors.

Race test (`-CitixRaceTest` on the server, two driving clients): joins both,
walks them through checkpoints. PASS = countdown, `Race P1/P2 finished`,
results cards, repeat race; zero errors.

Sprint test (C1 `-CitixNetWalk -CitixNetSprint`): PASS = server + remote views
both read vel 700 (not 340).

Latency budget (measured): movement snapshots ~20 Hz LAN; rendered position
tracks within ~30 cm at speed; at 120 ms injected lag (`net pktlag 120`) the
render stays smooth with delay ≈ lag + snapshot age. Server holds 33 ms frames
with 4 PCs + full traffic pool. No client prediction: owners feel RTT.
