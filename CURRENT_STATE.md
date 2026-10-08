# Citix — Current State

_Last updated: session 33. **Gap audit closed: independent records, per-shooter
combat, per-suspect police, authoritative shops, race loop, world identity,
death rules, listen parity, ped reactions, remote equipment, photo authority —
all verified live.**

## Where the project is

```
Seed
  -> Macro geography      (river)                   [DONE]
  -> District planning                             [DONE]
  -> Major arterials                               [DONE]
  -> Bridge planning                               [DONE]
  -> Secondary roads                               [DONE]
  -> Block detection                               [DONE]
  -> Parcel generation                             [DONE]
  -> Building placement                            [DONE]
  -> Landmarks                                     [DONE]
  -> Detail / props                                [DONE]
  -> Traffic / pedestrians / street lights          [DONE]
```

Press **Play**. To inspect the macro plan instead of the city, tick *Show Planning Preview*
on `ACitixCityGenerator`.

## Verification status

| Check | Result |
|---|---|
| `CitixEditor` + `Citix` targets build | ✅ Succeeded |
| Macro plan (`-CitixSelfTest`) | ✅ 8 districts, 44 roads, 4 bridges, 130 blocks, 168 nodes |
| Plan: illegal crossings / duplicates / degenerate cells | ✅ **0 / 0 / 0** |
| Plan: intersection angles | ✅ 0 below 60°; min **65°**, median **85°** |
| Plan: bridge squareness to the river | ✅ worst **0.2°**, mean **0.1°** |
| Plan: road network connectivity | ✅ 300 edges, **1 connected component** |
| Built city | ✅ 300 road segments, 160 junctions, 114 local streets, 130 blocks, **699 parcels, 564 buildings, 6 landmarks, 440 lamps** |
| Geometry safety | ✅ roads trimmed at every junction pad (no overlapping road meshes); buildings sit inside parcels inset from the roads (no building touches a road) |
| Road surface depth conflicts | ✅ carriageway trimmed to the pad at every junction, so no two road tops share a Z; riverside promenade sits in the plan's land gap and is measured per bank, so it never touches a road |
| Instance count / build time | ✅ **42,362 instances**, 99 ms |
| Performance | ✅ **87–129 fps** @1600×900 with 92 traffic + 57 pedestrians |
| Water | ✅ animated procedural normals flowing along the river; reflects the skyline; **no measurable frame cost** |
| Traffic following | ✅ speed-dependent time headway; tightest same-lane centre spacing **1755 cm** (car is 500 cm) — no overlaps |
| Traffic junctions | ✅ **0 cars stopped inside a junction**; a car no longer demands a stop when it is already past the stop line |
| Traffic turning (monitored) | ✅ one car logged 4×/s through real turns: crab angle max **26.5°**, mean **0.59°**, 4 samples in 217 above 20°; yaw rate 0 above 90°/s; straight-through exactly `crab=0, yawRate=0` |
| Traffic monitor | ✅ `Citix.Traffic` prints crab angle + yaw rate per car, the tightest spacing and cars stopped in a junction; `Citix.Traffic watch 1` / `-CitixTrafficWatch` logs one car's trajectory |
| Parked cars | ⏸️ **disabled** (`MaxParkedVehicles = 0`) for now; the kerbside parking system is intact and re-enables with one value |
| Pedestrian hitboxes | ✅ each walking pedestrian carries a 48×48×170 cm query-only box: **29 live** in a test, and hitting one knocks it down at any speed without stopping or damaging the car |
| Spawn gating | ✅ out-of-view + 24 m clearance + 18 m free lane ahead + local density cap; rejections logged `occupied=466 tooClose=3757 inView=557 noRoom=4` |
| Ragdoll grounding | ✅ bodies rest at **15.5–16.0 cm** on the pavement (pavement top is exactly 16); 3/3 knock-downs recovered, 0 lost |
| Road graph overlay | ✅ `Citix.RoadGraph 0–4`; built 1236 instances (mode 1) / 2760 + 168 labels (mode 3); **0 dead-end nodes** (degree 1/2/3/4+ = 0/8/56/104) |
| Car types | ✅ six profiles: 1100–8000 kg, 151–238 km/h, hull 320–760; suspension derived from mass |
| Drift per car | ✅ body slip in the same test: hatchback −35°, sedan −41°, van −24°, bus −12° |
| Handbrake | ✅ drifts again (slip −41°); **no engine force and no braking force** while held, so it costs acceleration, not speed |
| Hull strength | ✅ 100 → **400** (sedan), 320–760 across types; ~9 full-speed impacts to destroy a sedan |
| Low-speed immunity | ✅ no damage below **30 km/h** (833 cm/s); only 85 and 94 km/h impacts logged damage in the test run |

Screenshots: `Saved/Screenshots/WindowsEditor/City_overview.png` (the whole city, day) ·
`City_spawn_11.png` / `City_spawn_20.png` (street level, day and night) ·
`Water_11.png` / `Water_20.png` (the river, day and night).

## What changed this session

### 0. Session 35: chase loop closed and playable; car networking smoothed

**Loop closed.** The chase GameMode now gives each driver a lobby player start with
per-controller claims, so clients are possessed (before, `FindPlayerStart` failed and a
guest's car sat frozen as a simulated proxy). Client ready/relay/capture were gated on
the server-only auth GameMode; they now test the replicated `ACitixChaseGameState` and
use the Server RPCs. The ram "separation" rule is latched (a hit clears it; the pair must
move apart before the next hit), so four separate rams work instead of one. Replacement
cars now spawn free (they were flagged occupied, so the enter search skipped them).
Round transitions, relays, rams, wrecks and results log under `[CitixChase]`.

**Verified headlessly** (`-CitixChaseTest`, listen host + one guest): ready → countdown →
pursuit → 3 relays → exits unlocked → runner escape → round 2 with roles swapped → 4 rams
→ first wreck + 2 replacements → replacement claimed → 4 more rams → second wreck →
match → rematch. `-CitixChaseTestCapture` covers the capture win. Both clients agree on
phase/timer/relays/health/hits/role/rounds.

**Car display.** Every non-authority car copy is now driven by buffered snapshot
interpolation (time-stamped buffer, sample at `now - delay`, blend between the bracketing
snapshots, quaternion rotation, clamped extrapolation when starved) instead of per-snapshot
dead reckoning. Measured on a driving guest's own car: max per-frame step 19–46 cm → 5–15 cm;
reversals ~120 → ~35 per 2 s (`-CitixSmoothLog`; `-CitixLegacyInterp` selects the old path).

**Tools.** `Tools/chase_mp_test.ps1` runs host + guest(s) with these switches and collects
logs. It needs the local Zen storage service (the cooked project store) reachable — the
editor brings it up; without it the standalone exits before logging.

**Turning + particles.** Replicated rotation defaulted to one byte per axis (~1.4° steps);
pinned to short components (~0.006°) so turning is smooth (measured yaw step mean
0.01-0.04°, max ≤0.27°). Drift smoke / boost trail now read the replicated `NetState`
(`bHandbrake`, `LateralSlip`, `bBoosting`) on non-authority copies, so the guest sees its
own particles (verified `smoke=52 flames=38` while drifting).

**Still to verify:** 5-minute timeout expiry, mid-round disconnect, and two PCs on separate
machines.

### 0. Session 34: standalone startup crash fixed (engine/project build-flag mismatch)

**Symptom.** The Development standalone (`Binaries\Win64\Citix.exe`) exited with Unreal
status `777006` (`CrashDuringStaticInit`) before any map loaded. The editor was fine.

**Diagnosis (symbolized).** A debug capture pinned the fault to a dynamic initializer in
`Intermediate\...\UHT\CitixCitySettings.gen.cpp` → CoreUObject `RegisterCompiledInInfo` →
`TDeferredRegistry<FStructRegistrationInfo>::AddRegistration`, writing to `NULL + 0x10`.
The linked engine object (`Engine\Intermediate\...\UnrealGame\...\Module.CoreUObject.13.cpp.obj`)
took the `WITH_RELOAD` path (the `InfoMap` hash lookup), while the project's `Citix` module
was compiled with `WITH_LIVE_CODING 0` (no `WITH_RELOAD`). `WITH_RELOAD` changes the layout
of the UHT registration structs (`FStructRegisterCompiledInInfo`, `FClassRegisterCompiledInInfo`,
`FStructReloadVersionInfo`), so the engine walked the project's registration array with the
wrong stride and dereferenced a garbage info pointer.

**Fix.** Pin `bWithLiveCoding = true` in `Citix.Target.cs` and `CitixServer.Target.cs` so the
project module's `WITH_LIVE_CODING`/`WITH_RELOAD` matches the engine objects it links against.
Rebuilt; the project module now reports `WITH_LIVE_CODING 1`.

**Verification.** `Citix.exe -NoSound -NullRHI -stdout` runs past 30 s: city generated,
100 traffic vehicles active, no crash. The `777006` exit is gone.

**Still open.** Shipping-package launcher re-check (previously status `3`), and regenerate the
authored water/emissive materials (`CitixMaterialSetup`) — the runtime currently logs
"authored water material not found; water will be flat".

### 0. Session 33: multiplayer gap audit — everything first-priority, finished

**Scope.** `Docs/MULTIPLAYER_GAP_AUDIT.md` ranks 1–4 (P0), 6, 8, 10, 11, 13, 14,
16–19 implemented and tested; 9 (seed verify + single clock), 12 (input rate +
measured budget), 15 (logout cleanup), 20 (4-player measurement), 21 (server
target + names), 22 (session-only surfaced), 23 (docs). Deferred with reasons:
7 race (shipped — see below), co-op rosters, clock *handshake beyond* verify,
full vehicle prediction, menu UI, persistence.

**Ranks 1+8 (records).** `FCitixPlayerGame` fully integrated: money/score/heat/
loadout/jobs/activities/waypoint/quest/stunts/crash per controller, published
per owner (private fields owner-only); legacy globals mirror the focus record
so HUD/tests/host view are untouched; solo-owned objectives (no implicit
co-op); stunts/crash/photo/quest per record.

**Ranks 2+3 (weapons).** Per-shooter ammo/cooldown/reload/trigger/ADS; owner
snapshot closes the HUD path (loadout, reload fraction, ADS, stops); aim
validation (finite, normalized, 1000 cm origin bound — tuned from live
rejections at 832 cm); per-type trigger semantics (auto resend only); own
tracer on server echo; per-shot ADS spread.

**Rank 4+5 (crime).** Instigator identity through gunfire/ram/blast/ped-kill
window; per-offender heat/stars/escape; units carry suspects (never nearest
innocent); escape/bust/death/disconnect release per suspect; police cars get
client cosmetics + replicated disable; officers replicate (existence/pose/
death/stride); car gunfire uses the real target (+10 hull) and officer hits
draw blood (+10, catch pressure); per-suspect pursuit status replicates;
off-graph suspects get nearest-node spawns (fleeing the map is not an escape).

**Rank 6 (shops).** Browsing/selection local per driver; `ServerBuyShopRow`
validates proximity/funds/ownership/catalogue with buyer-only denials; equip
without recharge.

**Rank 7 (race).** Full loop: Y join/leave, roster, 3-2-1-GO countdown,
4 ordered road checkpoints (25 m), live positions, P1–P3 rewards once,
240 s timeout, DNF on leave/death/disconnect, race-again, X forfeit; per-racer
beacon/guide/HUD from owner snapshot; resets keep progress (documented rule).

**Rank 10 (death).** Respawn validates ground + headroom (retries, fallback);
2 s spawn protection; death fails job/activity; pursuit closes per victim.

**Rank 11 (vehicles).** Replicated owner/occupancy (prompts + remote audio);
exit speed gate (15 m/s) + capsule clearance sweep with denials; 5 s reset
cooldown (recovery paths bypass); drive inputs 15→30 Hz.

**Rank 13 (listen).** `CombatVerbAllowed` includes authority; host verbs/verbs
verified live (host walks + starts delivery, guest drives, mutual visibility).

**Rank 14 (peds).** Replicated reaction (walking/down/dead/recovering) with
canned remote poses; hitbox synced with visibility (no invisible blockers).
Also fixed: pedestrian spawning was fully dead (spawn-gating view state never
written since the multi-anchor migration) — 0 peds found by probe, 51–58
after the fix.

**Rank 16 (discovery).** Personal POI/district sets + rewards (OwnerOnly);
shared flags stay for map display; HUD counts read the owner snapshot.

**Rank 18 (equipment).** Per-pawn local gun props (remote kind from public
`VisibleWeapon`); remote wheels spin/steer from snapshot; engine audio by
replicated occupancy.

**Rank 19 (photo).** Server-validated rewards from submitted views (finite,
cooldown, 300 m bound); screenshot saves locally; cooldown replicates.

**Rank 9 (world).** City seed replicates; clients verify and disconnect on
mismatch; duplicate local clock retires when the server copy arrives; clock
always relevant; rain rides the clock (no separate path needed).

**Verification.** Isolation (earn/buy/heat/shop/dual-suspect/photo, B fixed),
PvP ram + gun kills, verb matrix, sprint, race countdown→P1/P2→repeat, SP
self/weapon/wanted PASS, 4-player server holds 33 ms, 120 ms lag stays smooth,
listen host+guest, kill→logout→rejoin stable with fresh record. Zero errors
throughout. Visual-only items (beam, bars, remote tracers/props, lightbar)
are structurally replicated; they need an interactive visual check.

### 0. Session 32: PvP health, rams, gunfire, respawn beam (+ SP-only audit)

**Audit first (Docs/MULTIPLAYER.md §5, untouched as requested).** 13 items:
shops/ammo economy, ADS feel, reload UX, car swap, police visuals, ragdoll
limbs, smoke/wheels on remotes, photo pause, time scrub, per-player progression
separation, district cards on clients, per-player kill heat, fall/blast damage,
death penalty.

**Implemented.** Health per player state (100, +6/s regen after 5 s quiet);
car-vs-walker rams server-side (50/hit, 1 s invuln, knock flying — drivers safe
in cars); guns via Server RPC with server resolution (shared arsenal, starter
pistol on MP join, per-shot ADS flag, shooter turn replicates); per-shooter
shot-event feed so remotes play local tracers; death killfeed + heat; respawn
on foot in a 30–100 m ring with a tall blue replicated beam (6 s fuse); health
bars (own stack + floating bars over other drivers); shared ammo mirrored to
client HUDs. No-drive-by rule moved to input/RPC layers (director core keeps
legacy behavior).

**Verification (live).** Ram kill (50→0, killfeed, respawn, 1 s spacing), gun
kill (34×3, killfeed, respawn ×2), seed agreement, zero errors. SP weapon test:
buy/fire/ammo/ADS/startle PASS; lethality=0 is a pre-existing screenshot-mode
quirk (no walker within 120 m at 5.8 s — ped spawn gating, untouched by this
work; needs no game-code change). Beam/bars/tracers are structurally replicated
but need an interactive visual check (headless cannot see them).

### 0. Session 31: sprint was local-only (remotes saw walking)

**Report.** Holding Shift: owner runs, other screens see walking, positions
diverge.

**Cause.** Sprint set `MaxWalkSpeed` on the local machine only. The server kept
340 while the owner predicted 700 — every remote saw walking, diverging
360 cm/s. (Jump needs nothing: stock CharacterMovement replicates it.)

**Fix.** Replicated sprint intent: local apply for prediction responsiveness,
Server RPC + `ReplicatedUsing` apply for server truth and remotes.

**Verification (live).** Server walker vel=700, seed unchanged (25700771);
remote view vel=700, position within smoothing lag. Zero errors. New
`-CitixNetSprint` hook (with `-CitixNetWalk`) covers the sprint leg.

### 0. Session 30: skin re-roll, movement matrix, editor on iGPU

**Skin.** On-foot body rebuilt from `FMath::Rand()` on every exit and differently
per screen: the seed was assigned after spawn, but BeginPlay builds the rig at
spawn. Now `SpawnActorDeferred`, seed set, then `FinishSpawning` — same body
every exit, identical everywhere (matrix run: seed 25700771 on server + both
clients). Car skins were already consistent (replicated type/paint).

**Movement matrix (live, server + walk client + drive client).** Exit RPC,
14 km walk tracked by server (340 cm/s), drive tracked both directions, parked
car matches to 2 cm on all instances, moving car within interp lag at 160+
km/h-Sep (cross-process log skew accounts for the rest). Zero errors.

**Perf (user's screenshot: 3 fps, iGPU 99%, 5070 4%).** They play in the editor
(PIE), and Windows had no GPU preference: the editor rendered on the AMD iGPU
while `-game` builds used the 5070. Pinned both editor exes to High
performance in `HKCU:...UserGpuPreferences` — editor restart required.
Advised: test MP via `-game` clients (PIE + editor overhead is far heavier),
`-CitixLowSpec` (now also kills motion blur), `t.MaxFPS 60`, close background
apps; external monitor bypasses iGPU scanout. The "running shown as walking"
and control delay are downstream of 3 fps: at 3 rendered frames/s no sync code
can look right.

### 0. Session 29: top speed depended on frame rate (physics dt clamp)

**Report.** 150 km/h at low fps drove like 50 km/h; audit of gameplay code
found all forces/timers correctly delta-scaled.

**Measurement (standalone A/B, full throttle, `-CitixHoldThrottle` probe).**
Top speed: 130 km/h at 133 fps, 64 at 15 fps — smooth systematic curves, not
traffic luck. Root cause is one engine default: Chaos clamps physics dt to
1/30 s without substepping, so below ~30 fps the car sim runs in slow motion.
Fix in `Config/DefaultEngine.ini` (substepping, 6x 1/60): 15 fps rose 64 to
90, and 30 fps now reaches the full 130. Residual 15 fps gap is coarse
per-frame control sampling — out of scope (unplayable for other reasons).

### 0. Session 28: movement stutter (link saturation + naive interp)

**Report.** Synced cars laggy and stuttery in both framerate and movement;
suspected iGPU. **Disproven:** client RHI log shows D3D12 adapter 0 is the RTX
5070 Laptop GPU and active (Aftermath/SER on). No setting changed.

**Real causes, both fixed and verified live.** (1) Snapshots arrived at ~14 Hz:
every-tick `NetState` publishes plus forced full GameState pushes at 4 Hz
saturated the link and starved movement. Now `NetState` at 15 Hz, GameState on
change only, pawn update 60 Hz → arrivals ~20 Hz (server sim itself runs
~30 Hz, so that is the practical ceiling). (2) Pure converge interp stepped
between snapshots. Now velocity extrapolation with age decay (parked cars rest,
snap beyond 8 m). Result: both client cars track the server within ~250 cm at
160+ km/h, clients at 90–130 fps with `-CitixLowSpec`, zero errors. New flag
`-CitixLowSpec` halves render cost for dual-window laptop testing.

### 0. Session 27: one-sided car freeze (replicated movement never applied)

**Report.** One screen showed a car moving, the other showed it frozen.

**Diagnosis (live, reproduced).** Server sim fine (input RPCs arrive, cars drive
60+ km); client copies frozen at spawn with correct roles and physics off.
Property replication healthy (`NetState` speed live on clients); only movement
never applied. Arrival counter proved fresh snapshots arrive ~14 Hz but the
actor never moves. Engine code (`ActorReplication.cpp`): movement apply runs
only for simulated proxies, and physics-packed snapshots feed only simulating
bodies — ours simulate server-side only. So owner copies (autonomous) AND
remote copies (physics path on non-simulating bodies) both froze. Movement
replication for cars had never actually worked; earlier checks only parked cars.

**Fix.** `InterpolateRemoteMovement` on every non-authority copy: converge to
the replicated snapshot, snap beyond 8 m (exits/resets). Traffic unaffected
(kinematic roots pack non-physics movement, engine apply works).

**Verification (live, 3 processes, both clients driving).** Both client cars
track the server within interp lag at 160+ km/h; sandbox census still agrees
everywhere; zero errors. Standalone untouched (fix is authority-gated).

### 0. Session 26: multiplayer stage 2 (shared-world verbs, tested live)

**Resumed mid-refactor.** The tree held a half-finished per-player rewrite of
the sandbox director that did not compile (new `AController*` signatures with
old definitions, deleted sim members, `Score` shadowing `APlayerState`). First
restored a building tree: legacy single-player sim back intact, per-player
types kept as an additive replication layer.

**Implemented (shared-world, server-authoritative).** `ACitixPlayerState`
(score, money, jobs, waypoint, job, activity + stops, card/toast serials,
photo, loadout) and `ACitixGameState` (POI flags, visited districts, shop
sites); server mirrors the sim into both at 4 Hz; clients never simulate
(director `Tick` gated on `NM_Client`) and each owns a presentation-only
director (non-replicating, deterministic local city) rendering the snapshot
through the existing HUD fields (route/beacon/guide rebuilt locally, cards and
toasts shown once each on local timers). Waypoint/delivery/activity/cancel are
Server RPCs; job progress follows the nearest driver; combat/shop/car-swap stay
single-player until stage 3.

**Verification (live, 3 processes).** Server saw both verb RPCs and started the
delivery; server + both clients agreed (`wp=Delivery job=1 score=350
discovered=7/31`, markers 76 server / 70 each client — routes differ only by
start pawn); stage-1 loop intact (2 PCs + 2 pawns, positions matched to the
centimetre, traffic replicated); zero errors/fatals/ensures. Standalone
self-test PASS with clean shutdown.

### 0. Session 25: multiplayer stage 1 (server-authoritative, tested live)

**Analysis first.** Full player-0 audit (67 sites), replication audit (zero
existed), plan in `Docs/MULTIPLAYER.md`: per-system server/replicate/RPC/owner
table, what breaks with 2 PCs, stages 1–4.

**Implemented.** Dedicated-server model: replicated vehicle pawns (server Chaos
sim, input RPCs at 15 Hz, 30 Hz transform + HUD/audio state) and on-foot pawns
(CharacterMovement prediction, deterministic appearance); Server-RPC enter/exit
with car ownership; 8-start ring with per-controller claims freed on logout;
traffic/peds simulate server-side on multi-driver anchors with union pooling;
TimeOfDay/weather replicate (advance server-side); client city bootstrap
(deterministic local copy for rendering + collision); TakeVehicle destroys
(replicates); photo-pause and time-scrub authority-gated; shared verbs gated to
single-player until stage 2.

**Verification (live, 3 processes).** Join on separate starts, mutual visibility
with position-matched pawns, full-speed predicted walking tracked by the server,
exit RPC + possession transfer, drive RPC moving the server car, timeout logout
with claim release, clean rejoin, zero crashes. SP regressions PASS, seed exact.

### 0. Session 24: hit sparks, no gun ragdolls, vanish fixes

**Hit feedback.** A pooled spark system (24 code-driven puffs, gravity + shrink,
no assets): 3 per gunshot in the tracer color, 4 on kills and ram contacts,
6 on a disabled unit.

**Ragdoll rules.** Guns never ragdoll: grazed walkers flee, lethal hits collapse
in place (stand-and-fade, counted for heat like before). Being shoved on foot
staggers instead of ragdolling. Only vehicles (player car, traffic, blasts) send
bodies into physics.

**Vanish fixes.** Two real ones: recycled deaths kept zero-scale invisible parts
forever (walked as ghosts) — reset now restores visibility; and get-ups could
start up to 4 cm under the pavement — they now start exactly on top, with
visibility forced through the whole handoff.

**Verification:** weapon PASS (collapse kill), ragdoll PASS (car knockdowns still
tumble 3/3), wanted PASS (heat via collapse deaths), sandbox clean. Seed exact.

### 0. Session 23: smooth pursuit, officers on foot, ramming, wreck ejection

**Pursuit feel.** Cars now glide per-frame with eased headings instead of 4 Hz
steps (the judder), hold short of the player instead of clipping inside
(1800 on foot, bumpers at 420 in a car), and ram: bumper contact damages the
player's hull 12/s while the rammer takes 4/s (it can wreck itself).

**Officers.** A car close to an on-foot player dismounts a walking officer (cap 2)
that closes to 12 m, faces the threat, shoots pistol tracers inside 30 m, and
tightens the catch. Officers die to gunfire like pedestrians (same hits-to-kill,
fall + fade, +35 heat) and stand down when stars clear or you drive off.

**Wrecks.** Explosions are 4-stage (white flash, big swell fireball, rising smoke
column, expanding shockwave disc) over ~1.8 s, and the blast ejects the driver on
foot beside the wreck. Wrecks persist 8 s before repair.

**Verification:** wanted PASS (closing 13389→7997, officer spawn/kill/release),
blast PASS (driver ejected), sandbox clean. Seed 1337 exact.

### 0. Session 21: combat, police and pacing pass

**Guns.** Tracers start at the barrel tip (crosshair stays honest from the camera);
the on-foot body snaps to the camera yaw on every shot and stays glued while
aiming. Gun proc tucked closer to the chest. Damage is per-gun hits-to-kill on the
existing fade-out path: pistol 3, SMG 4, rifle 2, sniper 1 (100 health).

**Police.** 130 km/h pursuit that repaths to the player every second, spawns
10–200 m out of view, shows as flashing red squares on the minimap, and shoots
back inside 40 m with star-paced cooldowns (1.2/0.9/0.6 s) and spread
(5.5/4/2.5°): hull damage in the car, tightening catch pressure on foot. Police
cars take the same hits-to-kill and go dark/disabled when destroyed (replacements
keep spawning while stars hold).

**Pacing.** Dawn (4.5–7.5) and dusk (16.5–19.5) run 3x longer, night keeps its
stretch, day runs 2x faster.

**UI.** Discovery/district cards moved to the top of the screen; player minimap
dot tripled with a white core and long heading tick; shop UI has clickable BUY /
LEAVE buttons next to the rows (E / X / ESC still work).

**Verification:** self/activity/weapon/wanted/sandbox/on-foot all PASS, including a
live lethality kill (hits=1 kills=1), proven pursuit closing (13302→9324 m) and a
screenshot-verified held gun. Seed 1337 exact.

**Follow-ups fixed after first test.** Pursuit ping-pong: repathing from the car
re-anchored to the node just left behind, so units oscillated instead of chasing —
repaths now hold the target node (sticky path). Spawns intercept ahead of motion
and cap at 400 m fallback. Gun placement: the pawn origin is the capsule centre,
not the feet — the prop now rides beside the chest with a glowing sight, verified
in screenshots.

### 0. Session 20: delivery routing, shop mouse UI, on-foot rules

**Guide everywhere.** Deliveries and quests now lay the same silent waypoint route
as activities, so chevrons + beacon + route line follow with no map open. Chevrons
thinned to 7 x 1.2 m. `G` can no longer hijack an active delivery route.

**HUD.** Progress (discovered/score/funds/contract/route) moved to the top-left,
out of the driving view; job/activity trackers shifted below it.

**On-foot rules.** Talking, activities, firing, ADS and reload need feet on the
ground — no drive-bys, no in-car prompts. Entering/exiting, the map and photo mode
close the shop first.

**Shop UI.** Fullscreen takeover: bigger panel/rows/fonts, mouse cursor with hover
highlight, click-to-select, `E` buy, `X`/`ESC` leave. Pawn movement and camera are
frozen while it is open.

**Verification:** activity/weapon/sandbox/walk all PASS, seed 1337 exact.

### 0. Session 19: in-scene route guide + minimap declutter

**Route guide.** A pooled instanced actor lays glowing chevrons along the active
route (128 cap, re-laid at 4 Hz, colored like the objective: orange delivery,
cyan/pink/yellow tour, cyan drive, teal saved waypoint). Deliveries and quests
can be driven with no map open. Hidden with no route.

**Declutter.** The minimap now shows only roads, the tinted route line, the saved
waypoint, the delivery/quest destination and the gunsmiths. POI dots, crossing
markers and tour-preview markers are gone from the driving view (the full map
keeps POIs as the deliberate reference). The minimap-detail unlock was removed
with them; the activity-variant unlock stays.

**Verification:** activity PASS with live guide counts (33/42 chevrons, 0 after
arrival), sandbox regression clean, seed 1337 exact.

### 0. Session 18: waypoint colors

**Objectives are color-coded everywhere.** Manual waypoint green, delivery orange,
drive route cyan, tour stops cycling cyan → pink → yellow per stop. The 3D beacon
pillar re-materials on every start/stop-advance, route lines and minimap/full-map
markers follow the objective color, upcoming tour stops preview in their own
colors, and the activity panel edge matches. Delivery and tour never share a color
at the same time (mutually exclusive by design).

**Verification:** activity + sandbox regressions PASS, seed 1337 exact.

### 0. Session 17: real shops + building hitbox fix

**Storefronts.** Each gunsmith is now a solid sidewalk shop, not a person in the
road: brick body + metal counter block cars/feet/bullets, with a lit window, door,
slanted awning, sign poles and a warm glowing sign. The keeper stands behind the
counter facing the street. Sites snap to the road node, push to the sidewalk side
with the most carriageway clearance, and face the road.

**Hitboxes.** Found it: collision is per-surface, and style-rolled masses
(`GlassGold` bodies, `Roof`/`RoofDark` caps, `Spire`/`Dome` shafts, landmark `Orb`
bulbs) were missing from the collide table — any building rolled in those styles
had no hitbox at all. All of them now collide. Window light-bands stay
non-colliding on purpose (coplanar ribbons on facades).

**Verification:** drive (crash damage at 89/61 km/h proves live collision,
81–117 fps), weapon economy PASS, sandbox regression clean, seed 1337 exact.

### 0. Session 16: weapon economy (guns, shops, money, kill heat)

**Guns.** Pistol ($250, semi), SMG ($600, auto), rifle ($1200, auto), sniper ($2500,
scoped) from a shared catalogue. Hitscan with per-gun spread, tracer bolts, mag +
limited reserve, 1.4 s reloads (manual `T`, auto on empty). The player starts
unarmed; firing unarmed only shows the way to a gunsmith.

**ADS + scope.** Hold `RMB` (or gamepad R3): crosshair tightens, the visible gun
prop slides to the view centre, and every pawn camera FOV eases down per gun. At
near-full ADS the sniper draws a fullscreen scope overlay with mil lines.

**Shops.** 10 gunsmiths (one per district centroid + landmark overflow) snapped to
road nodes, each with a stationed employee (character-library rig, rest pose, no
tick) and a warm glowing sign. `E` talks, `1-5` selects, `E` buys/equips, `X`
leaves. Ammo refills per gun. Shops are always on the minimap/full map.

**Money.** Deliveries pay cash mirroring the score reward; one 3x priority contract
per in-game day (midnight wrap detection, `J` accepts). Killed pedestrians use the
existing lethal fade-out (no gore) and each adds +35 heat through a death counter,
so killing sprees bring the same police chase as before.

**Verification:** self-test PASS (10 shops, seed exact); weapon economy PASS
(buy/funds/ammo/ADS/reactions); sandbox regression clean. Wanted loop re-tested
after the refactor (see below).

### 0. Session 15: exploration, activities, photo scoring, pulse tool, wanted loop

**Exploration.** Discoveries grant +50 once and raise a centred landmark card
(name + district flavour + discovered x/y); crossings keep the old toast. District
entry raises a one-time card per district (name + flavour + visited x/y) via the
plan's own boundary polygons. 8 discoveries unlock faint undiscovered-landmark dots
on the minimap; 3 completed activities unlock rain/rush/night route bonuses.

**Activities.** `E` near a POI starts a contextual activity (tour near undiscovered
landmarks, else a drive route); one activity at a time, guarded against delivery
jobs and vice versa. Objective panel shows name, stop progress or timer, distance,
off-route warning and cancel hint. `X` cancels cleanly. Routes reuse the waypoint
Dijkstra path and beacon; getting lost only triggers a nudge (route rebuilds every
tick). Tour: 3 stops, +60/stop, +120 completion. Drive: timer, 120 + time bonus,
plus rain (+40), rush (x1.5) and night-waterfront (+60) variants when unlocked.

**Photo mode.** HUD hides as before, plus a one-line controls hint and last-photo
score. `ENTER` scores the view (landmark cone + golden/night + weather + waterfront,
cooldown 8 s, each capture granted once) and saves a `HighResShot`.

**Pulse tool.** `LMB/RT` fires a pooled emissive bolt (8 actors, tick only in
flight) with hitscan effect: pedestrians flee (new `FleeFrom`, no damage, no
ragdolls), traffic pulls over (new per-agent `StopTimer`), police hits escalate.
Cooldown 0.45 s with a HUD bar. Public use = heat.

**Wanted/police.** Heat 0–100, stars at 25/55/85 with hysteresis, decay when quiet
and far. 1–3 capped `ACitixPoliceVehicle` units (traffic-car body + flashing
emissive lightbar, no lights) spawn on road-graph nodes out of camera view and
pursue via 1 Hz staggered Dijkstra. Caught (slow, close, 3 s) relocates to a safe
road node with score −100. HUD: star pips, SEARCHING/PURSUIT, escape bar.

**Verification (all headless, seed 1337 exact):** self-test PASS; activity slice
PASS (discover→route→complete, score-once); cancel/restart + photo + rain PASS;
weapon PASS (flee observed); wanted PASS (heat→3 stars→pursuit→caught→reset);
sandbox/drive/walk/ragdoll regressions clean. Drive perf 75–120 fps with rendering
matches the 87–129 baseline. New switches: `-CitixActivityTest`,
`-CitixWeaponTest`, `-CitixWantedTest`. Controls recorded in `Docs/CONTROLS.md`.
Progress is session-only (no pre-existing save path — reported limitation).

### 0. Road surfaces no longer z-fight, traffic plans ahead, ragdolls stay visible

**Road depth conflicts.** Every carriageway was drawn the full edge length, so all the roads
meeting at a junction put their top faces on exactly the same Z and z-fought across the whole
crossing — this is the "roads clipping into each other" that was visible everywhere. The
carriageway is now trimmed to the junction pad exactly like the sidewalks and markings, so the
single oriented pad owns the crossing and there is one road surface per Z. Separately, the
riverside promenade used to be drawn straight through the riverside road (both 16 cm tall, same
Z) along the entire waterfront: `PlanRiverGap` is now 2800 cm and the promenade width is
*measured per bank* from the nearest road corridor, so where the plan's jitter brings a road
close the promenade simply narrows instead of overlapping. Promenades are also skipped where a
bridge crosses the bank, so a deck is never buried under pavement.

**Traffic now plans instead of reacting.** See `ARCHITECTURE.md` for the design. In short: each
car walks its lane up to three edges ahead every frame (via a per-frame edge→agents bucket) to
find the nearest vehicle, the sharpest corner, and the next junction's light/queue state; it
then takes the minimum of its eased free-flow speed, a corner braking curve, a junction limit,
a **speed-dependent time headway** following limit and the player-avoidance limit. A **hard
anti-overlap clamp** guarantees a minimum bumper gap regardless of tuning, corner speed is
held briefly after a turn so the car cannot floor it mid-corner, and steering aims at a point
up the committed path so it starts turning *before* the corner. Spawning is gated on clearance
and lane space and biased toward the roomiest candidate, with a local density cap so traffic
cannot pool around a stationary player. The old failures — accelerating mid-turn, 9 m gaps at
any speed, invisible cross-junction leaders, cars spawning inside each other, steering snapping
to edge directions — all came from the same few lines and are gone.

**Knock-downs no longer sink out of sight.** Ragdoll limbs collided as `PhysicsBody`, andpavements/kerbs deliberately ignore that channel (the player's car is also a physics body, so a
16 cm kerb must never stop it). A knocked-down body therefore fell straight through the slab it
landed on and rested on the bare ground plane 16 cm below, which the pavement then hid — the
"disappears then stands up" that was reported. Limbs now collide as `WorldDynamic` (which
pavements block) and road surfaces block characters/dynamic bodies too, so a body rests on the
road it was hit on. Verified: bodies settle at 15.5–16.0 cm on a 16 cm pavement. Also fixed: a
hit could write to whichever pedestrian had been swapped into an agent slot by a budget
release, and a victim culled as "distant and unseen" a moment before the hit could stay hidden
for the whole sequence.

**The road graph is now visible.** Agents walk a graph that was completely invisible while
playing — edge indices and lane offsets, never geometry — which is why "why is that car doing
that" has been hard to answer. `ACitixRoadGraphDebug` draws it in world space, instanced (a
dozen draw calls, no per-frame cost) and rebuilt only when the mode changes: edges coloured by
road class and wider for majors, a direction chevron per edge, node markers sized and coloured
by degree, and (mode 2+) the lane centrelines for both directions — the exact geometry traffic
drives on, which makes the lane-offset discontinuity at a junction plainly visible. Modes 3–4
add node and edge labels. Bridges are magenta; **dead ends (degree 1) are drawn red and large**
and the build logs the degree histogram: this map has **0 dead ends** (degree 1/2/3/4+ =
0/8/56/104), which rules out "an agent with nowhere to go but back" as a cause of odd turns.
Turn it on with `Citix.RoadGraph 2` (works in PIE, which is where F8 leaves you), by ticking
*Road Graph Debug* on `ACitixCityGenerator`, or with `-CitixRoadGraph=2` at launch.

### 1. Every car type has its own performance

The six car types (`ECitixCarType`) were only different to look at — they all shared one set of
physics numbers. They now each carry a `FCitixCarPerformance` profile (mass, top speed, engine
force, brake force, hull, tyre grip, and how they drift), applied by
`ACitixVehiclePawn::ApplyCarPerformance` whenever the car is built or swapped. That covers
**taking over a parked or traffic car**, so the attribute system applies to any car the player
drives, not just their own.

| Car | Mass | Top speed | Hull | Handbrake grip | Drift |
|---|---|---|---|---|---|
| Hatchback | 1100 kg | 227 km/h | 320 | 0.34 | loosest, most willing |
| Sedan | 1500 kg | 238 km/h | 400 | 0.40 | the reference |
| Pickup | 2000 kg | 220 km/h | 480 | 0.46 | heavier, resists rotation |
| Van | 2400 kg | 202 km/h | 460 | 0.58 | understeers rather than drifts |
| Truck | 5000 kg | 166 km/h | 700 | 0.70 | barely drifts at all |
| Bus | 8000 kg | 151 km/h | 760 | 0.78 | practically refuses to rotate |

**Suspension is derived from the mass** rather than hand-tuned per car
(`SuspensionStiffnessPerKg`, `SuspensionDampingPerKg`, `MaxSuspensionForcePerKg`), so every car
rides at the same height and simply carries its own weight instead of a bus bottoming out on
sedan springs. The coefficients reproduce the original sedan values exactly.

Measured, same drift test in each car (throttle from 2 s, handbrake at 4 s):

| Car | Speed at handbrake | Body slip | Tyre slip |
|---|---|---|---|
| Hatchback | 76.7 km/h | −34.9° | 1038 cm/s |
| Sedan | 77.0 km/h | −41.1° | 1132 cm/s |
| Van | 59.8 km/h | −23.7° | 730 cm/s |
| Bus | 41.1 km/h | −11.7° | 380 cm/s |

The bus reaches half the speed in the same window and slides a third as far — the difference is
obvious from the driver's seat. The HUD now names the car (`HULL - SEDAN`).

New test switch: **`-CitixCar=<name>`** starts the run in a given type (e.g. `-CitixCar=Bus`),
so each profile can be measured with the same test.

### 2. Drifting is back, and the handbrake costs acceleration rather than speed

An earlier change cut drive *and* applied handbrake braking, which turned Space into a brake
pedal and killed the drift. It now does what was asked:

- **Drive is cut to zero while the handbrake is held** (`HandbrakeEngineForceScale = 0`), so you
  cannot accelerate out of a drift. That is the whole cost.
- **The handbrake applies no braking force at all** (`HandbrakeBrakeScale = 0`), so it does not
  work as a brake pedal either.
- **The slide is restored**: rear grip on the handbrake back to `0.40` (it had been raised to
  `0.80`, which is why drifting had gone), yaw damping back down to `0.80`, full steering
  authority, and only a light stability assist so it drifts rather than spins.

Measured (sedan, throttle held throughout): handbrake at **77.0 km/h**, body slip **−41.1°**,
55 smoke puffs. The speed decays to 39 km/h over that period from tyre scrub and drag, which is
the physics of sliding rather than an added brake. Taking a corner on the handbrake is now a
genuine trade-off: you keep the rotation, you lose the ability to accelerate.

### 3. Hull strength ×4 and a 30 km/h damage floor

- **Health is 300 % higher** — 100 → **400** for a sedan, and per-type from 320 (hatchback) to
  760 (bus). Read literally, "300 % higher" is ×4, which is what I used; it is one value per
  profile if you meant ×3.
- **Collisions below 30 km/h do no damage at all** (`CrashSpeedThreshold` 350 → **833 cm/s**,
  from about 12.6 km/h).
- Damage is still capped per crash (`CrashDamageMax` 45), so it takes roughly **nine** full-speed
  impacts to destroy a sedan, and a bus shrugs off far more. Above about 57 km/h every crash
  costs the same capped 45, which is what makes cars feel durable.

Verified by logging every damaging impact: driving a bus through the city for eight seconds
(hull 100 % throughout, bumping kerbs and scraping at low speed) and then two real impacts at
**94 km/h** and **85 km/h**, each costing the capped 45 (hull 400 → 355 → 310). No impact below
30 km/h produced a damage line.

One exploit closed while doing this: swapping car types used to hand back a full hull, which
would have made `V` a free repair. The hull **fraction** now carries over when a profile is
re-applied.

### 4. Water: rebuilt with layered waves, a macro breakup field and fresnel

The river material was **rebuilt**, not retuned.

**What was wrong with it.** The previous version summed three sine waves at fixed frequencies,
directions and speeds. That is a *periodic* field, and a periodic normal field produces periodic
specular highlights — which is exactly what read as "obviously tiled" and "repetitive bright
dots". Nothing varied across the surface either: roughness was a single value, the motion was
three constant scrolls, and the base colour was flat. It also stayed mirror-sharp head-on, which
is the opposite of how water behaves.

**What it is now** (`M_CitixWater`, authored by the same `CitixMaterialSetup` commandlet as the
window and emissive materials):

- **Ten waves in three layers** — swell, wind chop and fine ripple. Wavelengths are deliberately
  incommensurate (79 cm to 15 m) so the combined field has no short repeat, and the ten
  directions are spread around the compass.
- **Real dispersion.** Each wave's phase speed follows `c = sqrt(g·lambda/2pi)`, so long waves
  travel faster than ripples and the layers never lock into step with each other. That is what
  removes the mechanical, synchronised feel. `SwellSpeed`, `ChopSpeed` and `RippleSpeed` scale
  the three layers independently on top of the global `WaveSpeed`.
- **A macro breakup field** built from four very long waves (70–240 m) drives three
  *uncorrelated* channels: ripple strength, glossiness and apparent depth. Some patches of water
  are choppier, some glossier, some darker, so the surface never behaves uniformly.
- **No tiling at all.** Everything is evaluated from world position, so the field is unique
  across the whole map by construction rather than by hiding a repeat.
- **Fresnel-driven roughness**: mirror-sharp at grazing angles (`RoughnessAtGrazing`, 0.032) and
  softer and more broken head-on (`RoughnessMax`, 0.165). That is the physically correct response
  and it is what fixes the flat look when looking down at the water from a bridge.
- **Distance softening**: roughness rises with pixel depth, so sub-pixel ripples on distant water
  are averaged away instead of crawling. This replaced the shimmer that distant water had.
- **Depth feel**: the base colour lerps between a deep tint and a shallower, richer tint using the
  macro field, so the river is not one flat blue.
- Reflections still come from **Lumen**, which the project already runs, so they cost nothing
  extra. The material's job was to stop feeding Lumen a regular highlight grid that it then
  mirrored.

**Cost.** No textures at all: ten cosines, four sines and roughly 250 ALU per pixel, which is
cheaper than sampling three normal maps and cannot tile. The frame-time log is in the same range
as before (60–96 fps across these runs, which vary with traffic and pedestrians).

**Exposed parameters**, all with tuned defaults: `WaterColor`, `WaterShallowColor`, `WaveScale`,
`WaveSpeed`, `WaveStrength`, `SwellSpeed` (1.0), `ChopSpeed` (1.15), `RippleSpeed` (1.30),
`MacroScale`, `MacroSpeed`, `MacroStrengthMin` (0.55), `MacroStrengthMax` (1.45), `RoughnessMin`
(0.050), `RoughnessMax` (0.165), `RoughnessAtGrazing` (0.032), `FresnelPower` (4.0),
`FresnelBase` (0.02), `ReflectionStrength` (1.0), `DistanceRoughness` (0.22), `DistanceRange`
(30000).

**Inspecting it.** `-CitixScreenshot -CitixWaterView` looks down the channel from 34 m;
`-CitixWaterView=700` does the same from 7 m, which is the driver's-eye view. Screenshots:
`Water_final_day.png`, `Water_final_night.png`, `Water_final_aerial.png`.

**Remaining limitation.** The wave field is analytic, so it has no true aperiodic structure — at
the largest scales the macro waves are still recognisable sine shapes. Authored tiling noise
normal maps blended at several scales would add real organic structure. The material is
structured so that swapping the analytic field for texture layers is a contained change.

### 5. The city is built ON the plan, never beside it

`FCitixCityBuilder` reads the plan and the road graph and emits geometry. It invents no layout of
its own:

| Stage | Source |
|---|---|
| River, embankment parapet, riverside promenade | the plan's river |
| Carriageways, sidewalks, curbs, markings, bridges, railings | the plan's road graph edges |
| Junction pads, zebra crossings, signals | graph nodes with degree ≥ 3 |
| Block platforms | the plan's blocks, inset by the roads that bound them |
| Parcels | those platforms, subdivided along the block's own orientation |
| Buildings | parcels, by the parcel's **density zone** |
| Landmarks | only inside the primary cluster, clustered at its core |
| Lamps, trees, AC units, shop signs | road class and district rules |

**Geometry safety is structural, not hopeful.** Roads are trimmed at each junction pad by the
incident corridor half-width, so no two road meshes overlap. Parcels are inset from the roads
that bound them, so no building can touch a road. Buildings are placed inside their parcel and
the whole lot is rotated into the block's frame in one place (`RotateBoxesIntoBlock`), so the
building generator never has to know about the plan.

### 6. Density and skyline come from the plan's zones

Height bands are per zone, not per district, which is what makes the towers *group*:
PrimaryCluster 130–340 m (landmarks to 520 m), SecondaryCluster 70–170 m, High 32–110 m,
Medium 14–62 m, Low 8–30 m. Zone also picks the archetype family (towers / mid-rise / slabs /
warehouses), so the skyline reads as planned rather than scattered.

### 7. Level 3 of the hierarchy actually exists now

A block more than three lots deep gets **mid-block local streets** cut through it, splitting it
into sub-cells — 114 of them across the city. That is where the third level of the road hierarchy
appears, exactly as a real superblock works.

### 8. Traffic, pedestrians and street lighting are back

All three run on the new road graph. Lamp heads recorded during road building feed the pooled
night lighting; landmark positions feed the building light emitters.

## Known issues and honest limitations

1. **Diagonal streets are 0 %.** A straight road crossing an orthogonal grid always produces an
   intersection angle below 60°, so "10–15 % diagonal" and "angles 60–120°" cannot both hold at
   grade. This build chose the angle rule. Options: accept Broadway-style 45° junctions, make
   diagonals grade-separated once there is elevation, or give quarters their own bearing (needs a
   much finer lattice — tried, it folds cells at this resolution).
2. **Curved share is 43 %** (target was 20–30 %). The river is structural and the roads that
   follow it curve; that was explicitly asked for.
3. **Mid-block local streets are geometry only** — they are not in the routing graph, so traffic
   uses arterials and secondary roads. Adding them means appending graph nodes.
4. **The city is a band along the river** (~1.5 km × 0.9 km), not a 4–6 km disc.
5. **No elevation.** Bridges are decks at street level; no flyovers, tunnels, interchanges.
6. **Bridges are narrow visually** (they reuse the arterial carriageway) and have railings but no
   separate structure.
7. **Blocks still read a little empty.** `EmptyLotChance`/`ParkChance` leave gaps, which is
   realistic but the block platforms are large.
8. **No art pass** — no textures, no authored meshes, no star field, procedural-only audio.
9. **Traffic vehicles do not use the per-type performance.** Only the car the player drives does;
   traffic keeps its own cruise speeds. Making traffic type-aware would mean threading the
   profile into the traffic system's speed model. It also does **not** overtake: a car stuck
   behind a slow vehicle or a parked one queues rather than changing lane.
10. **Traffic has no intersection reservation.** Cars stop before a junction whose exit queue is
    backed up, and lights gate the approaches, but two cars from different approaches can still
    cross at the same moment on green.

## Immediate next actions

1. **Art pass**: façade/window textures, a star field, real character meshes. Highest impact.
2. **Decide the diagonal question** (limitation 1) — it needs a design decision, not code.
3. **Add local streets to the routing graph** so traffic can use the full hierarchy.
4. **Elevation**: add a Z/level field to `FCitixPlanRoad` for flyovers and grade-separated
   diagonals, then lift the outer ring highway onto a structure.
5. **Scale**: 2 km², then World Partition + HLOD with the chunk actor as the seam.
6. **Two-way traffic collision**, so traffic yields physically to the player.

## Earlier systems (retained and working)

- **Vehicle**: hand-built physics car, chase/hood cameras, boost + fire trail, hull damage,
  explosion, synthesised engine audio (no assets), six type profiles.
- **Characters**: low-poly humanoid rig with procedural walk and a pose system; on-foot player
  with sprint/jump.
- **Crowd**: line-of-sight-gated pooling, cheap per-citizen health, physics ragdolls for every
  knock-down (resting on the surface they land on), articulated get-up animation.
- **Traffic**: planning agent simulation — cross-junction look-ahead, speed-dependent headway,
  hard anti-overlap clamp, corner speed hold, path-following steering, clearance-gated spawning.
- **World**: day/night cycle with district lighting profiles, a restrained 4K HDR night sky that
  fades in after dusk, wet
  reflective streets, rain weather driving wetness/fog/ambient, pooled street/building lights.
- **Sandbox**: discovery/POIs, delivery jobs, stunt scoring, city map with routing, photo mode,
  rush-hour density.
