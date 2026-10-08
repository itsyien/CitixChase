# Citix multiplayer and gameplay-loop audit

**Inspected:** September 29, 2026.  
**Target:** a multiplayer GTA-style low-poly sandbox: each player owns their inventory, money, wanted stars, and progression; players share one authoritative world, fight each other, and race together.  
**Scope:** recommendations only. No existing project files were edited.

## Main finding

Citix has a usable multiplayer foundation, but its gameplay is still largely a **shared single-player simulation**. Separate `PlayerState` objects do not currently mean separate player systems: `MirrorToReplication()` copies the director's global money, arsenal, heat, objectives, and rewards into every connection.

There is already an `FCitixPlayerGame` structure containing personal money, weapons, wanted state, and objectives. However, source searches show no gameplay callers of `PublishPlayerGame()`, and `FindPlayerGame()` is only called by its own pawn overload. The main tick and controller RPCs continue to use the old global fields. This is unfinished migration work, not a reason to build a second inventory/economy framework.

The largest missing loops are:

- **Personal progression:** A acts → only A's state changes → B remains independent.
- **Combat economy:** earn money → buy/equip a weapon → fire/reload → replenish ammo → continue fighting.
- **Personal crime:** identified offender → offender's stars → visible police pursue that offender → escape/bust/death closes that pursuit.
- **Competitive racing:** invite/join → ready/countdown → validated checkpoints → results/rewards → return to free roam.

“Not complex” is interpreted here as shallow or incomplete gameplay, as well as small implementation gaps. Complexity itself is not the goal: a small system with clear outcomes, recovery, and repeat play is better than a large unfinished one.

## Evidence and limits

This is a **source inspection**, not a fresh multiplayer playtest or performance benchmark. I inspected `Docs/MULTIPLAYER.md`, project goals/current-state/roadmap/architecture/controls, active C++ gameplay systems, replication declarations, RPC callers, and configuration. The default configured mode is `CitixDrivingGameMode`; stock shooter/horror variants are not proof that those features work in the Citix sandbox.

Binary Blueprint assets were not inspected in the editor. “Absent” below means no implementation was found in the inspected active C++ path, configuration, or documentation. Runtime-sensitive findings are explicitly marked **Verify**. Previous “IMPLEMENTED + TESTED” claims in `MULTIPLAYER.md` remain historical claims, not new test results from this audit.

## What already exists and should be retained

| Area | Existing foundation | Important boundary |
|---|---|---|
| Networking | Server RPCs, replicated player/game state, server-controlled simulation | Personal gameplay state is still globally mirrored |
| World | Deterministic procedural city generated locally on each machine | Requires matching seed/settings/build; no authoritative configuration handshake found |
| On-foot movement | `ACharacter`, appearance replication, sprint RPC/replicated sprint state | Do not replace built-in movement with custom networking |
| Vehicles | Server physics, input RPCs, remote transform interpolation, hull/boost display state | No client prediction; input/ownership/recovery rules need completion |
| Ambient population | Server traffic/pedestrian pools anchored across connected players; appearance/visibility replication | Reaction poses, collision lifecycle, and separated-player density need work |
| Environment | Replicated clock hours and rain intensity; local weather presentation | Client clock bootstrap can create a duplicate local clock; verify binding/relevancy |
| PvP | Server-resolved on-foot gunfire, car-to-player ram damage, individual health/regen, respawn and beam | Shared ammo/cooldowns; incomplete HUD/loadout wiring and damage coverage |
| Jobs/activities | Delivery and drive/tour success, failure, cancellation, route/beacon, rewards | One shared objective; closest player can complete it for everyone |
| Wanted | Heat decay, 0–3 stars, police pursuit, bust/reset | Global crime state and incomplete remote police presentation |

“Everything synchronized” should mean identical **gameplay facts**: actor existence, position, damage, inventory transfers, purchases, objectives, outcomes, and police targets. Static buildings need not be transmitted individually when generation is proven identical. Camera, HUD layout, rain streaks, smoke particles, and individual ragdoll limbs can be local presentation driven by authoritative state.

## Priority list — greatest impact first

**P0:** blocks independent/fair multiplayer. **P1:** major missing loop or shared-world reliability. **P2:** polish, scale, or release readiness. **P3:** lower immediate gameplay impact.  
**Effort:** S = focused wiring/rule change; M = several related paths; L = a new loop or substantial networking work. These are relative estimates, not promised schedules. Ordering is by impact; dependencies are described below.

| Rank | Priority | Work needed | Status | Effort |
|---|---|---|---|---|
| 1 | P0 | Finish independent player state throughout the simulation | Partial structure; global runtime | L |
| 2 | P0 | Separate and correctly display weapon/ammo/reload/ADS state | Shared server state; missing client copy | M |
| 3 | P0 | Validate client shot origin and combat requests | Server resolves hits but trusts submitted origin | M |
| 4 | P0 | Attribute crimes and pursuits to the correct player | Global heat; nearest-player targeting | L |
| 5 | P1 | Complete police cars/officers and pursuit-state synchronization | Partial; docs understate car replication | M |
| 6 | P1 | Enable authoritative shops and close the money/ammo loop | Standalone-only purchases | M |
| 7 | P1 | Add an actual shared competitive race loop | Missing in active sandbox | L |
| 8 | P1 | Give jobs, waypoints, activities and contracts explicit owners | Shared; RPC initiator discarded | M |
| 9 | P1 | Establish authoritative world configuration and one client clock | Configuration gap; clock risk | M |
| 10 | P1 | Close player death, respawn and environmental-damage rules | Basic respawn exists; partial damage coverage | M |
| 11 | P1 | Finish vehicle occupancy, entry/exit safety and fair reset/input rules | Partial | M |
| 12 | P1 | Make driving responsive under latency | Interpolation only | L |
| 13 | P1 | Support the listen-server host through the same gameplay paths | Standalone-only gates block host verbs | M |
| 14 | P1 | Synchronize pedestrian knockdown/death/recovery and pool collision | Appearance/transform only; collision risk | M |
| 15 | P2 | Define disconnect/rejoin ownership and state cleanup | Start claims cleaned; remaining policy incomplete | M |
| 16 | P2 | Separate personal discovery/rewards from shared city knowledge | Shared flags and reward bookkeeping | M |
| 17 | P2 | Make prompts, notifications and objective feedback truthful online | Partial; disabled actions still advertised | S–M |
| 18 | P2 | Show each player's equipped weapon and remote driving effects | Single weapon prop; remote effects incomplete | M |
| 19 | P2 | Make photo mode safe online and close its scoring loop | Local capture; unpossess/repossess path | S–M |
| 20 | P2 | Protect shared-world density, visibility and performance | Multi-player anchors exist; global caps | M |
| 21 | P2 | Add normal host/join/disconnect UX and a distributable server path | Console/editor launch workflow | M |
| 22 | P3 | Decide and implement persistence if progress must survive reconnects | Session-only; no save path found | M–L |
| 23 | P3 | Align project documentation with the new vision and actual code | Contradictory/stale | S |

## Detailed findings and minimum completion conditions

### 1. Finish independent player state throughout the simulation

**Confirmed.** The director owns global `Money`, `Score`, `Heat`, loadout, active job/activity, waypoint, and event feed. Its tick processes all drivers against those fields; `MirrorToReplication()` broadcasts the same values to all player states. `FCitixPlayerGame` and its publisher exist but are not integrated. [E1]

This also affects transient state: `UpdateStunts()` is called for every car but shares `bWasGrounded`, `CurrentAirTime`, and `AirStartLocation`; `UpdateCrashWatch()` shares last hull fraction/cooldown across drivers. One player's grounded car can interfere with another's jump, and differences between two cars' hull fractions can be mistaken for a new crash. [E2]

**Minimum completion:** reuse the existing personal structure, key it by controller/session identity, and pass the acting player through every state-changing verb and reward/damage event. Move transient stunt/crash bookkeeping into that player's record too. Preserve shared actor simulation separately. Publish each player's own snapshot. Replicate private inventory/objectives only to their owner where appropriate; expose public health/appearance/wanted information deliberately.

**Done when:** A earns money, changes weapon, starts a delivery, jumps a car, or crashes; B's wallet, ammo, objective, stunt state, and stars remain unchanged. Simultaneous actions are independent.

### 2. Separate and correctly display weapon/ammo/reload/ADS state

**Confirmed.** `ResolveShot()`, `StartReload()`, and `UpdateWeapon()` use one `CurrentWeapon`, magazine/reserve array, reload timer, and firing cooldown. A firing can consume B's ammunition or block B's next shot; A reloading affects everyone. Multiplayer starter pistol grant is also global, so later joins do not inherently get a fresh personal supply. [E3]

There is an additional client wiring gap: `MirrorToReplication()` writes loadout fields into `PlayerState`, but `UpdateFromReplication()` does **not** copy weapon ownership, ammo, current weapon, reload state, or ADS into the client director that the HUD reads. A replicated field alone does not close the display path. The per-player publisher also omits `ActivityStops`, although route rendering consumes it. [E1, E4]

ADS is similarly incomplete: the client latches an aim flag, but returns before the director's ADS blend/camera path. Server shot resolution temporarily sets `bAdsHeld`, while spread is calculated from `AdsAmount`, so the submitted flag does not directly establish the intended spread reduction. A replicated `bReloading` flag also cannot drive the existing timer-based reload bar without corresponding presentation state. [E3, E4]

**Minimum completion:** personal server weapon timers/ammo; a complete owner snapshot; local ADS/FOV/reload presentation; per-shot authoritative spread derived from validated aim state. Send held-trigger updates according to weapon type: the current client resend loop runs for every gun, including semi-automatic weapons. Preserve repeatable trigger semantics.

**Done when:** two players shoot/reload simultaneously without affecting each other, receive their own starter loadout, see correct ammo and reload progress, and observe honest aimed/hip-fire behavior.

### 3. Validate client shot origin and combat requests

**Confirmed.** `ServerFireWeapon()` forwards the client-supplied camera location/direction directly to `ResolveShot()`. The resolver checks pawn type, ammo and global cooldown, but no bound relating that camera origin to the shooter's actual pawn/camera was found. It traces from the submitted point. Server-side damage resolution is therefore not sufficient to prevent a fabricated firing origin. [E3]

The PvP fallback uses a 150 cm proximity radius around the ray segment. This is simple and inexpensive, but needs obstruction/edge tests so targets beside cover do not take implausible hits. Own-client tracers play immediately before acceptance, so rejected shots can still look successful locally.

**Minimum completion:** validate finite values, legal origin offset, direction, living shooter, equipped weapon, fire interval, and request rate; resolve line of sight on the server. Add lightweight accepted-hit feedback and authoritative correction for rejected shots. Add lag compensation only after baseline aim validation and measured latency tests.

**Done when:** altered origins cannot shoot through walls or from another location; spam cannot exceed fire rate; moving targets and cover behave consistently for both players.

### 4. Attribute crimes and pursuits to the correct player

**Confirmed.** `AddHeat()` is global; pedestrian kills are detected through a total death counter with no offender identity. Player death receives a killer name string, not a stable attacker reference for personal heat. Police spawn around rotating driver anchors and hunt the nearest driver rather than an identified wanted suspect. One bust resets global heat/stars, releases the whole fleet, and subtracts shared score. [E5]

**Minimum completion:** carry instigator/controller identity through gunfire, ram damage, pedestrian damage/death, and police interactions. Store wanted timers/stars per offender. Give each police unit an explicit suspect; allow retargeting only under a defined rule. A suspect escaping, dying, disconnecting, or being busted must not clear another player's pursuit.

**Done when:** A commits a crime beside innocent B; A gains stars, police pursue A, and B can remain innocent even when physically closer to a unit. Two wanted players can escape independently.

### 5. Complete police synchronization and targeting

**Confirmed partial implementation.** `MULTIPLAYER.md` says police cars do not replicate, but `ACitixPoliceVehicle` inherits `bReplicates` and movement replication from `ACitixTrafficVehicle`. Its base car can therefore replicate. However, `InitializePolice()` is called only from the server spawn path: lightbar/livery initialization and flash-enabled state have no equivalent client initialization/replication path. `ACitixPoliceOfficer` has no replication setup. [E6]

`PoliceFireAtPlayer()` selects player 0 even though the caller calculated a nearest target; car damage can therefore go to a different driver than the aimed location. On-foot police hits tighten a catch timer instead of using player health. Client wanted-state text is derived from the local director's `PoliceUnits`, which are not reconstructed from a pursuit snapshot. [E5, E6]

**Minimum completion:** initialize replicated police cosmetics on every instance; replicate officer existence, pose/death and suspect; publish wanted/search/pursuit status for the owning player. Feed the actual target to police damage. Local cosmetic tracers can remain local, driven by server events.

**Done when:** server, A and B agree on police cars/officers, lightbars, targets, disabled units, gunfire consequences and pursuit status. Late joiners see the current pursuit.

### 6. Enable authoritative shops and close the economy loop

**Confirmed.** Deliveries grant money, but `InteractShop()` is blocked unless standalone. No multiplayer buy/equip/ammo RPC was found. `BuyWeapon()` and `BuySelected()` mutate the director's global money/arsenal. Online players can spend the shared starter ammunition without a working replenishment path. [E7]

**Minimum completion:** keep browsing/selection UI local; send shop ID and item/action to the server. Server validates proximity, living player, weapon ownership, valid catalogue entry, price, funds, ammo capacity and repeat requests before atomically deducting/granting. Equip already-owned guns without charging again. Preserve denial feedback.

The existing “inventory” is four weapons plus ammunition, not a general item bag. Independent weapon ownership/ammo is the minimum required now. Add item stacks/pickups/drop/trade only when concrete gameplay needs them; each transfer would require authoritative ownership and exactly-once consumption.

**Done when:** A earns and spends their own money, B is unaffected, both can replenish independently, and remote/out-of-range purchases or repeated requests cannot duplicate items or deductions.

### 7. Add a real competitive race loop

**Absent in inspected active path.** `ECitixActivityType` contains `Drive` and `Tour`. Existing route activities are single shared destination/tour challenges, not races: no participant roster, readiness, countdown, individual checkpoint/lap progress, finishing order, or race result/reward settlement was found. [E8]

**Minimum completion:** one race on an existing road route, with explicit invite/join/leave, participant roster, shared server countdown, ordered checkpoint validation per racer, results, one-time rewards, timeout/DNF/disconnect handling, and return to free roam. Track server time and crossings; clients never submit “I won.” Start with one race type and no betting/league system.

Define collision and reset rules: free instant resets during a race must not remove competitive consequences; vehicle swaps must not silently change the agreed car class.

**Done when:** A and B race the same course, see the same countdown/results, cannot skip checkpoints, receive only their own result/reward, and can start another race after a finish or DNF.

### 8. Give jobs, waypoints, activities and contracts explicit owners

**Confirmed.** Objective RPCs log the sending controller, then call parameterless director methods. Job/activity start selects player 0; progress follows whichever driver is nearest the shared waypoint; any client can cancel the shared objective. The same single daily contract is consumed globally. [E8]

**Minimum completion:** solo contracts belong to their initiator, with personal timers/routes/rewards/cancellation. For co-op, explicitly create a shared activity with participants and reward rules; unrelated nearby players must not advance it. Keep solo/co-op choice visible. Do not imply co-op merely because everyone can accidentally complete the same task.

Timed delivery currently checks arrival before timeout; define deadline precedence so an expired delivery does not succeed with an unintended reward. Drive/tour activities pay score but no money; that can be valid if the UI explains it. Make tour completion text match the actual accumulated reward rather than the fixed “+180” message. [E8]

**Done when:** B can start/cancel their own job without touching A's, and only invited members advance a co-op activity. Success/failure/cancel grants and clears state once.

### 9. Establish authoritative world configuration and one client clock

**Confirmed configuration gap; Verify clock behavior.** Client bootstrap builds from local project settings; generation uses local seed/overrides. `GameState` has no replicated city seed/settings/version/checksum. Matching defaults are an assumption, not a negotiated world identity. [E9]

`EnsureLocalWorld()` also spawns a local clock if the replicated one has not arrived. That local actor cannot become the server actor just because its class replicates. `ACitixTimeOfDay::Find()` returns the first matching actor; the local clock does not advance on clients. Weather, lights and HUD can bind to the wrong clock. The clock is not explicitly always relevant, despite controlling the whole city. [E10]

**Minimum completion:** transmit authoritative generation identity before enabling play, verify compatible settings/content, and use one unambiguous replicated clock or shared environment snapshot. Keep weather particles local. Late join and travelling far from the clock's location must retain current world state.

**Done when:** a deliberately mismatched client is corrected or clearly rejected; late joiners see the current city/time/rain; each client has one effective authoritative environment source.

### 10. Close death, respawn and environmental-damage rules

**Confirmed partial loop.** Personal health/regen and server respawn exist. Respawn uses a random ground-traced ring and `AlwaysSpawn`, without a full safe-space selection or post-spawn protection rule. The old pawn is destroyed before checking whether replacement spawn succeeds. [E11]

Car blasts damage pedestrians but not player health; police on-foot hits affect catch pressure rather than health. The active player class has no custom fall/blast-health path. Guns intentionally only fire on foot and do not damage drivers through their cars; no gun-to-player-car hull damage was found in the resolver. These restrictions need explicit product decisions, not silent expansion. [E3, E6, E11]

**Minimum completion:** define lethal hazards, damage instigators, occupant exposure, death consequences, job/race interruption and safe respawn; validate space/ground and recover from failed spawn. Decide whether nearby respawn is protected from immediate repeat kills. Keep existing free respawn if desired, but make it a clear rule.

**Done when:** each damage source follows the chosen rules, a failed/blocked spawn does not strand a player, and death cannot duplicate rewards or break an active job/race.

### 11. Finish vehicle occupancy, interaction safety and fair input/reset rules

**Confirmed partial implementation; Verify contact edge cases.** Server enter/exit and traffic takeover exist. Cars are protected from other players by `OwningController`; GTA-style theft of another player's parked car is therefore a missing rule if desired, not just missing replication. `OwningController` and `bOccupied` are not replicated fields; client interaction prompts cannot reliably distinguish personal/occupied cars using those fields. [E12]

Exit uses `AlwaysSpawn` at a fixed side offset. A comment mentions settled movement, but no explicit speed/clearance check is present in `RequestExitVehicle()`. Vehicle reset is a callable server action that stops motion without a cooldown or race rule. Drive RPC inputs have a stale-input watchdog, but boost/handbrake latch loss also deserves a packet-loss test.

**Minimum completion:** replicate public occupancy/owner identity, retain server possession authority, use collision-safe exits, define occupied-car entry/theft/passenger policy, and prevent reset abuse during competitive activities. Reject invalid drive inputs and safely release all held controls after missing input/disconnect. Reuse the takeover path; do not build a garage system first.

**Done when:** simultaneous enter requests yield one driver, prompts agree with server decisions, unsafe exits recover cleanly, and packet loss/reset spam cannot produce unfair or stuck control.

### 12. Make driving responsive under latency

**Confirmed limitation.** The car sends input at approximately 15 Hz and the server simulates physics; clients interpolate replicated movement. This keeps authority coherent but the owning driver waits for the round trip before seeing the result. That matters much more once racing and combat involve another player. [E13]

**Minimum completion:** measure current behavior under latency/loss first, then add the smallest compatible prediction/reconciliation approach for the owning vehicle. Remote vehicles keep interpolation. Validate steering, boost, collisions, braking and corrections together; smoother rendering alone does not remove input delay.

**Done when:** the chosen supported network conditions permit controlled cornering and close racing with bounded corrections, rather than obvious delayed steering. Record the supported player count and latency budget.

### 13. Support the listen-server host through the same gameplay paths

**Confirmed path mismatch.** Client firing/reload sends RPCs, but local host firing/reload reaches `CombatVerbAllowed()`, which returns true only in standalone. Shared snapshot presentation and remote shot-feed playback also run only on `NM_Client`. A host can therefore have different input and HUD behavior from guests. [E4, E7]

**Minimum completion:** make local host input invoke the same authoritative verbs and personal-state presentation as remote clients. Keep dedicated-server support. Use an explicit local-controller check for camera/UI instead of equating “client net mode” with “has a local player.”

**Done when:** host and guest can both shoot, reload, buy, earn, get wanted, receive notifications and race with the same rules. Test host exit and guest reconnection separately.

### 14. Synchronize pedestrian reactions and pool collision lifecycle

**Confirmed pose gap; Verify collision symptom.** Pedestrians replicate appearance, visibility and actor movement, but ragdoll kind/recovery pose/death state are local fields. Limbs move on the server while the root can stay at the standing position. Remotes cannot reconstruct the authoritative knockdown/death/recovery merely from root movement. [E14]

Traffic/pedestrian visibility notification paths hide visuals but do not repeat all server collision/hitbox state changes. Check whether recycled hidden actors leave client-side blockers or hitboxes, particularly during traffic takeover. Do not label this runtime symptom proven until tested.

**Minimum completion:** replicate a compact reaction state and timing, with a simple remote fall/get-up pose if full limb networking is too costly. Synchronize collision activation with pool activation/recovery. Preserve server damage decisions.

**Done when:** both players agree that an NPC is walking, down, dead or recovering, and hidden/recycled actors cannot block movement or falsely absorb shots.

### 15. Define disconnect/rejoin ownership and cleanup

**Confirmed partial cleanup.** `Logout()` frees start claims. No custom reconciliation for parked owned vehicles, personal director records, active objectives/pursuits, or reconnect identity was found. `FindOrSpawnPlayerVehicle()` can reuse an available car, but this is not an explicit abandonment policy. [E12, E15]

**Minimum completion:** choose what happens to the departing player's car, job/race seat, police targets, inventory and session state. Remove stale personal records and shot-feed entries. If reconnect retention is intended, restore by stable identity rather than controller pointer or join order; otherwise communicate that reconnect starts fresh.

**Done when:** repeated leave/rejoin does not grow abandoned actors/state, grant duplicate starter rewards, preserve invalid ownership, or stall a race. This does not require durable saving yet.

### 16. Separate personal discovery from shared city knowledge

**Confirmed.** Shared POI discovery and visited districts prevent later players from having their own first-discovery progression. `UpdateDiscovery()` awards global score; current district and card cooldown also belong to the shared director while drivers can be in different districts. [E1, E16]

**Minimum completion:** keep POI definitions/locations shared, but store personal discovered/visited sets and reward debouncing per player if exploration is part of their independent progress. A team discovery layer can remain optional. Resolve district name locally from the owner's location or publish its personal value.

**Done when:** A visiting a landmark does not consume B's discovery reward; players in different districts see their own location cards and completion counts.

### 17. Make online prompts and event feedback truthful

**Confirmed.** The HUD can show a gunsmith/activity prompt, but E routes through the standalone-only `InteractShop()` gate; online contextual activities are blocked along with shops despite having a `ServerStartActivity()` method. Action gates fail silently. The wanted label depends on unreconstructed local police arrays. [E4, E6, E17]

Card/toast replication keeps only the latest text and serial; several events between updates can overwrite each other. Different discovery/district serial counters are multiplexed into one card serial, so equal values can suppress a distinct card. These are gameplay-feedback gaps, not a reason for a general messaging framework. [E1]

**Minimum completion:** route E to the enabled contextual action; hide or clearly explain unavailable actions; display server denial/accepted-hit/purchase feedback. Use a coherent personal event sequence, plus public kill/race events. Add a bounded event queue only if real bursts lose meaningful notifications.

**Done when:** every advertised action works or explains its denial; unrelated players do not receive each other's private cards; important back-to-back outcomes remain visible.

### 18. Show remote equipment and driving effects coherently

**Confirmed.** Each director creates one nonreplicated `WeaponProp` and updates it for player 0. Client directors return early from simulation, with no per-player equipped-weapon presentation path found. Tracer feeds exist, but the other player's held gun, aim/reload pose and muzzle need a corresponding presentation source. [E3, E18]

Remote wheel/drift effects are documented as incomplete. Engine audio uses a cast to the pawn's `PlayerController` to decide whether it is driven; remote controllers are not a reliable public occupancy source on another client. [E13]

**Minimum completion:** build local weapon visuals for each relevant pawn from public equipped/aim/reload state; use replicated occupancy and display speed/steer/boost to animate wheels, sound and lightweight smoke locally. Do not replicate every particle or cosmetic component.

**Done when:** another player visibly holds and fires the correct gun, their muzzle matches the tracer, and their car reads as moving/driven rather than sliding silently with frozen wheels.

### 19. Make photo mode safe online and close scoring

**Confirmed path gap; Verify possession behavior.** Photo capture scores and increments the local client director with no authoritative capture RPC; the next snapshot overwrites personal photo totals/score. Its client director does not tick photo cooldown down. Photo mode also calls `UnPossess()`/`Possess()` from the local controller path, which requires dedicated/listen-server lifecycle testing. [E19]

**Minimum completion:** use a local view target/input suppression without changing authoritative possession. If photo rewards remain, submit a bounded view request and validate cooldown/world/view constraints server-side; keep screenshot saving local. Define that the shared world and vulnerability continue while in photo mode.

**Done when:** entering/exiting photo mode cannot strand or desynchronize the pawn, later captures work, and accepted rewards survive the next replication update without affecting B.

### 20. Protect density, visibility and performance with separated players

**Confirmed design ceiling; Verify actual budgets.** Population anchors already cover multiple players, but vehicles/pedestrians share fixed global caps. Players far apart divide the same pool. Visibility approximates cameras from pawn facing, so backward/look-around/photo views can differ from the server's conservative view model. Player pawns are always relevant, which is sensible for the current small session but not automatically scalable. [E20]

**Minimum completion:** test 2–4 players both together and widely separated; inspect population fairness, recycling visibility, server tick cost and bandwidth. Choose a bounded per-region allocation within an explicit total budget. Update camera hints only if observed popping justifies it. Avoid a replication-graph/Mass/streaming rewrite before measurements require one.

**Done when:** one player cannot drain the useful population from another's area, recycling does not visibly remove nearby actors, and the target player count holds a documented frame/network budget.

### 21. Add normal host/join/disconnect UX and distribution

**Absent in inspected active path.** Current multiplayer instructions launch an editor executable/server and clients by address. Default `GameInstance` is the engine class; no active create/join session or server-browser flow was found. The source has game/editor targets but no dedicated server target. That does not invalidate editor-based server testing; it leaves distribution unfinished. [E21]

**Minimum completion:** simple host/join-by-address or chosen session service, connection/loading/error/retry states, player name, leave-to-menu, and packaged client/server verification. Do not add matchmaking, friends, accounts and voice before a basic joining loop works.

**Done when:** another player can join without editor commands, understand a failed connection, leave, and retry without stale UI/game state.

### 22. Decide whether progression persists

**Confirmed session-only design.** No active save/load or player-state copy/reconnect restoration implementation was found. Separate inventory/money/stars inside a live session does not automatically imply persistent accounts. `MULTIPLAYER.md` explicitly describes no save system. [E15, E21]

**Minimum completion if required:** stable identity, server-owned save/load, explicit reconnect retention, atomic write/failure behavior and versioned data. Decide which transient values, such as stars/jobs, reset and which survive. Otherwise clearly communicate session-only progress and defer saving.

**Done when:** either reconnect/restart restores the intended personal values safely, or the session-only rule is explicit. Persistence should not delay live-session isolation, shops and races.

### 23. Align documentation and remove misleading assumptions

**Confirmed.** `PROJECT_GOAL.md` still excludes combat/crime, while the requested target includes PvP and stars. `MULTIPLAYER.md` mixes a historical “zero networking” inspection, implemented networking, later PvP work, and unimplemented stage labels. It understates inherited police-car replication and describes an eight-start ring while code creates a lateral line. Controls also advertise standalone-only behaviors without clear online availability. [E22]

**Minimum completion later:** update the vision, multiplayer ownership model, feature/status matrix, mode-specific controls and reproducible acceptance checks after implementation. Replace “implemented” with source-complete/runtime-verified statuses where needed. Keep template shooter/horror code outside the Citix feature checklist unless deliberately integrated.

**Done when:** the documents describe one consistent product and accurately identify online restrictions. This audit does not edit those files.

## Closed-loop assessment

| Loop | Current break | Smallest complete outcome |
|---|---|---|
| Personal progression | All players receive one global snapshot | Independent records, attributed mutations, correct owner presentation |
| Earn → spend → replenish | Online shops disabled; shared ammo | Personal server purchases/equip/refill with clear denial |
| Fight → damage → death → return | PvP exists; shared weapon timers, limited hazards, unsafe-spawn edge cases | Independent combat plus explicit, recoverable death rules |
| Crime → stars → police → escape/bust | Global heat, innocent nearest target, incomplete remote police | Attributed offender, visible targeted pursuit, personal resolution |
| Solo job → reward → next job | Global objective/cancel/reward | Owner-scoped completion/failure/cancel with exactly-once reward |
| Co-op job → shared result | Any nearby player implicitly advances it | Explicit participants and reward split, unrelated players excluded |
| Race → results → race again | No race system | One complete server-validated race including DNF/disconnect |
| Explore → discover → unlock | Shared discovery consumes later players' progression | Personal rewards/unlocks over shared POI definitions |
| Take vehicle → drive → exit/wreck → recover | Base loop exists; online occupancy/safety/fairness gaps | Public occupancy, safe exits, clear ownership/reset rules |
| Photo → capture → reward → capture again | Local score overwritten; cooldown not advanced locally | Local camera/screenshot plus accepted personal reward/cooldown |
| Join → play → leave → rejoin | Connection plumbing exists; lifecycle/product UX incomplete | Clean cleanup, clear state-retention policy and retry path |

## Small improvements worth doing without adding large systems

These are candidates after the relevant foundation is in place, not code changes made by this audit.

1. **Complete snapshot-to-HUD wiring:** copy existing weapon fields and publish activity stops; prove the whole state-to-display path. High impact, limited code surface.
2. **Fix online E routing:** do not let the shop gate swallow contextual activities; make prompts match supported actions.
3. **Initialize police cosmetics on remotes:** inherited car replication already exists; add the missing appearance/state wiring instead of a second police network system.
4. **Use the actual police shot target:** pass the selected pawn/controller instead of fetching player 0.
5. **Replicate public vehicle occupancy:** use it for enter prompts and remote audio/effects.
6. **Correct race-sensitive reset rules and safe exit checks:** focused guards around existing verbs, with visible denial feedback.
7. **Correct reward/deadline feedback:** tour totals, delivery timeout precedence, and clear success/failure/cancel outcomes.
8. **Fix local photo presentation/cooldown:** keep camera control local and separate reward authority.

## Suggested implementation sequence

1. **Independent two-player sandbox:** ranks 1–4, 8–9, and the state wiring from 17. Prove isolation with both players acting simultaneously.
2. **Complete repeatable combat/crime loop:** ranks 5–6, 10–11, 13–14 and 18. Two players can earn, buy, replenish, fight, get individually pursued, die/escape and continue.
3. **One complete race:** rank 7 with competitive reset rules and measured driving responsiveness from 12. Ship one reusable race before adding more activity types.
4. **Reliable everyday play:** cleanup/discovery/photo, normal joining, then measured scale tuning. Add persistence only if the intended retention policy requires it.

Do not rewrite the city, movement stack or entire director first. The main architectural work is to finish the existing personal-state migration and carry actor identity through real mutations. Reuse the existing road routes, vehicle fleet, weapon catalogue and job rules.

## Acceptance checks to use before calling multiplayer complete

Run with a dedicated server and two actual clients, then repeat relevant cases with a listen-server host and guest. Existing movement/verb/PvP test hooks are a starting point; the server-side PvP staging hook directly calls `ResolveShot()` and therefore does not by itself prove client input/RPC validation or HUD correctness.

- [ ] A earns/spends/shoots/reloads; B's funds, inventory, ammo and cooldown stay unchanged.
- [ ] A and B shoot simultaneously; semi-auto/full-auto semantics, displayed ammo, ADS and reload agree with server acceptance.
- [ ] Fabricated shot origin, invalid item, distant shop purchase and repeated buy requests are rejected without state corruption.
- [ ] A commits crimes near B; only A gains heat/stars and police target A. Two simultaneous suspects resolve separately.
- [ ] A, B and a late joiner see the same police/officer existence, disabled/dead state and world actors.
- [ ] A and B run separate jobs/routes and cancel independently; a co-op job accepts only its roster and pays once.
- [ ] A full race completes with ordered checkpoints/results; skipped checkpoints, reset abuse, DNF and disconnect do not corrupt results or rewards.
- [ ] Deliberately different city configuration is corrected/rejected; late join and distant travel preserve authoritative time/rain.
- [ ] Concurrent vehicle entry has one winner; prompts show correct occupancy; unsafe exits and packet loss recover safely.
- [ ] Guns, rams, police and environmental hazards follow explicit damage rules; blocked/failed respawn recovers and does not strand the player.
- [ ] Knocked-down/dead/recovering NPCs agree across screens; hidden pool actors have no client-side blockers/hitboxes.
- [ ] Death, escape, bust, job cancellation and leave/rejoin close only the affected player's state.
- [ ] Photo mode returns to normal play; accepted rewards survive replication; the shared world continues.
- [ ] Listen-server host and guest have the same available actions and correct personal HUD values.
- [ ] Repeated reconnects do not grow abandoned vehicles, personal records, shot-feed entries or active pursuits.
- [ ] At the chosen 2–4-player target, clustered/separated players and latency/loss tests meet recorded server/client/bandwidth budgets.
- [ ] Packaged join/error/leave/retry flow works without editor commands; session-only/persistent progress rules are clear.

## Source references

Line numbers refer to the files as inspected; function names remain useful if lines later move.

- **E1 — Personal-state migration and shared mirror:** [FCitixPlayerGame](E:/Citix/Citix/Source/Citix/Sandbox/CitixSandboxDirector.h:173), [FindPlayerGame / publisher](E:/Citix/Citix/Source/Citix/Sandbox/CitixSandboxDirector.cpp:254), [MirrorToReplication](E:/Citix/Citix/Source/Citix/Sandbox/CitixSandboxDirector.cpp:462), [UpdateFromReplication](E:/Citix/Citix/Source/Citix/Sandbox/CitixSandboxDirector.cpp:550), [main Tick](E:/Citix/Citix/Source/Citix/Sandbox/CitixSandboxDirector.cpp:3929).
- **E2 — Shared transient state:** [crash watch](E:/Citix/Citix/Source/Citix/Sandbox/CitixSandboxDirector.cpp:3183), [stunts](E:/Citix/Citix/Source/Citix/Sandbox/CitixSandboxDirector.cpp:3835).
- **E3 — Weapons and hit resolution:** [reload/resolve](E:/Citix/Citix/Source/Citix/Sandbox/CitixSandboxDirector.cpp:1888), [shot validation and trace](E:/Citix/Citix/Source/Citix/Sandbox/CitixSandboxDirector.cpp:1946), [weapon update](E:/Citix/Citix/Source/Citix/Sandbox/CitixSandboxDirector.cpp:2377), [starter pistol](E:/Citix/Citix/Source/Citix/Sandbox/CitixSandboxDirector.cpp:2226).
- **E4 — Client state/input paths:** [client Tick/feed](E:/Citix/Citix/Source/Citix/Player/CitixDrivingPlayerController.cpp:336), [fire/reload/ADS](E:/Citix/Citix/Source/Citix/Player/CitixDrivingPlayerController.cpp:826), [replicated player fields](E:/Citix/Citix/Source/Citix/Player/CitixPlayerState.h:12).
- **E5 — Wanted attribution and targeting:** [kill watch/heat](E:/Citix/Citix/Source/Citix/Sandbox/CitixSandboxDirector.cpp:2704), [busted](E:/Citix/Citix/Source/Citix/Sandbox/CitixSandboxDirector.cpp:2968), [wanted update](E:/Citix/Citix/Source/Citix/Sandbox/CitixSandboxDirector.cpp:3205), [police targets](E:/Citix/Citix/Source/Citix/Sandbox/CitixSandboxDirector.cpp:3423).
- **E6 — Police replication/presentation:** [police car inheritance/state](E:/Citix/Citix/Source/Citix/Sandbox/CitixPoliceVehicle.h:17), [police cosmetic initialization](E:/Citix/Citix/Source/Citix/Sandbox/CitixPoliceVehicle.cpp:14), [base car replication](E:/Citix/Citix/Source/Citix/Traffic/CitixTrafficVehicle.cpp:9), [officer constructor](E:/Citix/Citix/Source/Citix/Sandbox/CitixPoliceOfficer.cpp:5), [police gun target](E:/Citix/Citix/Source/Citix/Sandbox/CitixSandboxDirector.cpp:2637), [wanted display state](E:/Citix/Citix/Source/Citix/Sandbox/CitixSandboxDirector.cpp:929).
- **E7 — Shop and standalone gates:** [CombatVerbAllowed](E:/Citix/Citix/Source/Citix/Player/CitixDrivingPlayerController.cpp:677), [InteractShop](E:/Citix/Citix/Source/Citix/Player/CitixDrivingPlayerController.cpp:932), [buy/refill](E:/Citix/Citix/Source/Citix/Sandbox/CitixSandboxDirector.cpp:2567).
- **E8 — Objectives and existing activity types:** [RPC objective handlers](E:/Citix/Citix/Source/Citix/Player/CitixDrivingPlayerController.cpp:613), [delivery](E:/Citix/Citix/Source/Citix/Sandbox/CitixSandboxDirector.cpp:1217), [activities](E:/Citix/Citix/Source/Citix/Sandbox/CitixSandboxDirector.cpp:1397), [activity rewards](E:/Citix/Citix/Source/Citix/Sandbox/CitixSandboxDirector.cpp:1569), [activity enums](E:/Citix/Citix/Source/Citix/Sandbox/CitixSandboxDirector.h:85).
- **E9 — World bootstrap/configuration:** [EnsureLocalWorld](E:/Citix/Citix/Source/Citix/Player/CitixDrivingPlayerController.cpp:456), [generation seed](E:/Citix/Citix/Source/Citix/City/CitixCityGenerator.cpp:602), [GameState fields](E:/Citix/Citix/Source/Citix/Player/CitixGameState.h:15).
- **E10 — Environment:** [clock replication/tick](E:/Citix/Citix/Source/Citix/World/CitixTimeOfDay.cpp:26), [first-clock lookup](E:/Citix/Citix/Source/Citix/World/CitixTimeOfDay.cpp:442), [weather client binding](E:/Citix/Citix/Source/Citix/World/CitixWeatherSystem.cpp:277).
- **E11 — Death and blasts:** [DamagePlayer / RespawnPlayer](E:/Citix/Citix/Source/Citix/Sandbox/CitixSandboxDirector.cpp:2248), [vehicle explosion](E:/Citix/Citix/Source/Citix/Vehicle/CitixVehiclePawn.cpp:1086), [on-foot player](E:/Citix/Citix/Source/Citix/Character/CitixOnFootPawn.cpp:22).
- **E12 — Vehicle interaction/ownership/reset:** [ownership fields](E:/Citix/Citix/Source/Citix/Vehicle/CitixVehiclePawn.h:77), [entry/exit/takeover](E:/Citix/Citix/Source/Citix/Player/CitixDrivingPlayerController.cpp:1334), [input/reset RPCs](E:/Citix/Citix/Source/Citix/Vehicle/CitixVehiclePawn.cpp:793), [reset behavior](E:/Citix/Citix/Source/Citix/Vehicle/CitixVehicleMovementComponent.cpp:453).
- **E13 — Vehicle synchronization/presentation:** [vehicle Tick](E:/Citix/Citix/Source/Citix/Vehicle/CitixVehiclePawn.cpp:379), [interpolation](E:/Citix/Citix/Source/Citix/Vehicle/CitixVehiclePawn.cpp:600), [drive-input forwarding](E:/Citix/Citix/Source/Citix/Vehicle/CitixVehiclePawn.cpp:813).
- **E14 — NPC pose/visibility:** [pedestrian replicated fields vs ragdoll fields](E:/Citix/Citix/Source/Citix/Pedestrian/CitixPedestrian.h:116), [visibility/replication](E:/Citix/Citix/Source/Citix/Pedestrian/CitixPedestrian.cpp:236), [traffic visibility](E:/Citix/Citix/Source/Citix/Traffic/CitixTrafficVehicle.cpp:72).
- **E15 — Join/logout and tests:** [start/ownership/logout](E:/Citix/Citix/Source/Citix/Player/CitixDrivingGameMode.cpp:1504), [PvP staging test](E:/Citix/Citix/Source/Citix/Player/CitixDrivingGameMode.cpp:93).
- **E16 — Discovery:** [discovery/district updates](E:/Citix/Citix/Source/Citix/Sandbox/CitixSandboxDirector.cpp:3606), [shared city knowledge](E:/Citix/Citix/Source/Citix/Player/CitixGameState.h:15).
- **E17 — HUD and contextual verbs:** [HUD ammo/prompts](E:/Citix/Citix/Source/Citix/Player/CitixDrivingHUD.cpp:153), [E input](E:/Citix/Citix/Source/Citix/Player/CitixDrivingPlayerController.cpp:1043), [wanted/weapon HUD](E:/Citix/Citix/Source/Citix/Player/CitixDrivingHUD.cpp:748).
- **E18 — Weapon presentation:** [single prop creation](E:/Citix/Citix/Source/Citix/Sandbox/CitixSandboxDirector.cpp:168), [prop constructor](E:/Citix/Citix/Source/Citix/Sandbox/CitixWeaponProp.cpp:9), [follow/aim update](E:/Citix/Citix/Source/Citix/Sandbox/CitixSandboxDirector.cpp:2377).
- **E19 — Photo:** [local capture](E:/Citix/Citix/Source/Citix/Player/CitixDrivingPlayerController.cpp:1031), [photo mode possession](E:/Citix/Citix/Source/Citix/Player/CitixDrivingPlayerController.cpp:1196), [photo reward/cooldown](E:/Citix/Citix/Source/Citix/Sandbox/CitixSandboxDirector.cpp:1699).
- **E20 — Population/visibility budgets:** [traffic driver anchors](E:/Citix/Citix/Source/Citix/Traffic/CitixTrafficSystem.cpp:354), [traffic tick/pool](E:/Citix/Citix/Source/Citix/Traffic/CitixTrafficSystem.cpp:600), [pedestrian anchors/pool](E:/Citix/Citix/Source/Citix/Pedestrian/CitixPedestrianSystem.cpp:479), [settings](E:/Citix/Citix/Source/Citix/Core/CitixCitySettings.h:1).
- **E21 — Launch/distribution configuration:** [engine mode/GameInstance](E:/Citix/Citix/Config/DefaultEngine.ini:7), [game target](E:/Citix/Citix/Source/Citix.Target.cs:6), [dependencies](E:/Citix/Citix/Source/Citix/Citix.Build.cs:7), [multiplayer launch instructions](E:/Citix/Citix/Docs/MULTIPLAYER.md:1).
- **E22 — Product/docs:** [original goal](E:/Citix/Citix/PROJECT_GOAL.md:1), [multiplayer plan](E:/Citix/Citix/Docs/MULTIPLAYER.md:1), [controls/loop](E:/Citix/Citix/Docs/CONTROLS.md:1), [architecture/template boundary](E:/Citix/Citix/ARCHITECTURE.md:1).

