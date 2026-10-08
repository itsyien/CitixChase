# Citix — Architecture

## Module layout

Single runtime module `Citix` (`Source/Citix`), organised by domain. The stock
First-Person template code (`CitixCharacter`, `Variant_Horror`, `Variant_Shooter`,
`Variant_FirstPerson`) is **legacy template content** and is not used by the driving
game. It can be deleted once we are confident nothing references it.

```
Source/Citix/
  Core/
    CitixTypes.h              Enums + shared structs (districts, road classes,
                              surfaces, building specs, box instances)
    CitixCitySettings.h/.cpp  UDeveloperSettings — ALL city tuning (Project Settings
                              > Game > Citix City). No hardcoded magic numbers.
    CitixSurfaceLibrary.h/.cpp Maps ECitixSurface -> engine mesh + shared tinted
                              material instance + collision/shadow policy
  City/
    CitixCityPlan.h/.cpp      THE MACRO PLAN (planning stage). River, districts, arterials,
                              bridges, secondary roads, block detection, and a self-check.
                              Pure data; nothing here creates final geometry.
    CitixCityBuilder.h/.cpp   BUILD stage. Parcels, roads, junctions, river frontage, block
                              platforms, buildings, landmarks, street furniture - all read
                              from the plan, none invented. Emits box instances.
    CitixCityPreview.h/.cpp   Draws the plan as a debug diagram (coloured layers + labels).
                              A design tool, not the city.
    CitixRoadGraphDebug.h/.cpp Draws the road graph as a world-space overlay (edges by
                              class, nodes by degree, lane centrelines, labels). A
                              diagnostic tool: what traffic actually drives on.
    CitixRoadNetwork.h/.cpp   The plan converted into a routable graph for traffic/pedestrians.
    CitixBuildingGenerator    FCitixBuildingSpec -> instanced boxes. 13 silhouette
                              archetypes + palette/lighting. Used by the builder.
    CitixPropGenerator        Street lamps, trees, traffic signals, planters, AC units.
                              Used by the builder.
    CitixCityChunk.h/.cpp     Actor owning one spatial square of instanced mesh
                              components (one HISM per surface). The build target.
    CitixCityGenerator.h/.cpp Orchestrator: plan -> road graph -> ground -> city -> traffic
  Traffic/
    CitixTrafficVehicle       Presentation-only kinematic car actor
    CitixTrafficSystem        Player-centred agent simulation (lanes, following,
                              lights, spawn/despawn)
  Vehicle/
    CitixVehicleMovementComponent  Chaos rigid-body car: traced suspension +
                                   friction-circle tires. No authored content.
    CitixVehiclePawn               Chassis + low-poly car + chase camera + runtime
                                   Enhanced Input + enter/exit + drift smoke
    CitixCarLibrary                Low-poly car builder (6 types, high/low detail),
                                   shared by the player vehicle and traffic
    CitixDriftSmokeComponent       Pooled code-driven tyre smoke emitter
    CitixEngineAudioComponent      Procedurally synthesised engine note (USynthComponent)
  Character/
    CitixCharacterLibrary          Low-poly humanoid builder + procedural walk rig
                                   + the pose system used by the get-up animation
    CitixOnFootPawn                Third-person on-foot player (ACharacter)
  Pedestrian/
    CitixPedestrian                Presentation actor for one pedestrian
    CitixPedestrianSystem          Player-centred sidewalk walking population
  World/
    CitixTimeOfDay                 Day/night cycle: sun, moon, sky, fog, exposure,
                                   and emissive strength of windows/lamps/lights
    CitixWeatherSystem            Rain streaks + the weather cycle; drives wetness,
                                   fog, ambient and haze through CitixTimeOfDay
  Sandbox/
    CitixSandboxDirector          The "game" layer: points of interest / discovery,
                                   delivery jobs, stunt scoring, the navigation
                                   waypoint + road-graph route, and time-of-day
                                   traffic/pedestrian density
  Player/
    CitixDrivingGameMode      Bootstrap (city, lighting, weather, sandbox, spawn) + test hooks
    CitixDrivingPlayerController   Mode switching, map/job/waypoint/photo/car-swap verbs
    CitixDrivingHUD           Code-drawn speedometer, bars, minimap + full city map
```

## Generation pipeline

The city is planned first, then built, in explicit stages. The plan is pure data and is
validated before anything is drawn on it.

```
FCitixPlanGenerator            (City/CitixCityPlan.*)          [CURRENT PHASE]
    STAGE 1  Macro geography      river centreline (smooth spline), widths widening downstream
    STAGE 2  District planning    4-6 quarters as a partition of the river band
    STAGE 3  Lattice              rows along the river, columns across it, placed in river space;
                                  smooth structural nudge; outer rows form an irregular growth edge
    STAGE 4  Major arterials      riverside boulevards, strided rows, strided columns, bridges
    STAGE 5  Bridge planning      crossings only on arterial columns, so they are perpendicular
    STAGE 6  Secondary roads      the lines between the arterials, defining neighbourhoods
    STAGE 7  Local streets        [Phase 2: belongs with parcels]
    STAGE 8  Block detection      every lattice cell becomes a block with a district and a zone
    STAGE 9  Open space + zones   waterfront reserve, parks, and compact skyline clusters
        |
        +--> Validate()           crossings, duplicates, degenerate cells, intersection angles,
        |                         bridge squareness, connectivity, length mix.
        |                         If an attempt is dirty it is retried with a gentler district
        |                         rotation rather than shipped.
        v
FCitixRoadNetwork::BuildFromPlan        graph conversion (the plan's topology is already clean)
        v
FCitixCityBuilder::Build                the city ON the plan
        +--> river surface, embankment parapet, riverside promenade
        +--> roads: carriageway, sidewalks, curbs, median, markings; bridges get railings
        +--> junctions: oriented pads, one zebra per approach, signals
        +--> block platforms, inset by the roads that bound them
        +--> parcels, subdivided along each block's own orientation
        +--> buildings by density zone; landmarks only in the primary cluster
        +--> mid-block local streets (level 3) through blocks that need them
        +--> street furniture: lamps (with recorded heads), trees, signs, AC units
        v
ACitixCityGenerator
        +--> chunk actors (one HISM per surface per chunk) receive every box
        +--> ground slab sized to the plan
        +--> traffic, pedestrians, street lighting on the new graph
        +--> optional: ACitixCityPreview for inspecting the plan itself
```

Everything is deterministic from `UCitixCitySettings::Seed`.

## Key architectural decisions

### Plan before geometry
Separating the plan from the build is what makes a believable city tractable. The plan is where
"where should the river, the quarters, the avenues and the bridges be" is decided (authorable,
inspectable, validatable); the build is uniform machinery that consumes roads and blocks
without caring what shape they are. A quarter is a line in a table in `CitixCityPlan.cpp`.

### Structured irregularity, not randomness
The lattice is river-relative, so topology is orderly (every road is a chain of shared nodes,
nothing crosses anything, every intersection is a node) while the geometry follows the
geography. Irregularity comes from two smooth low-frequency waves plus a per-district
amplitude - never from white noise, which is what makes a generated plan look chaotic.

### The plan proves itself
`Validate()` measures the things that make a layout believable - accidental crossings,
degenerate cells, intersection angles, bridge squareness, connectivity - and the generator
retries itself until they pass. Layout quality is a build-time number, not an opinion.

### Roads at any angle
The graph stores no grid indices. Junction insets are baked per edge end (`TrimA`/`TrimB`),
intersections are oriented to the junction's own bearing, and buildings (Phase 2) will inherit
their block's orientation. Nothing assumes axis alignment.

### Instancing, not actors
Every piece of generated geometry is an **instance** in a
`UHierarchicalInstancedStaticMeshComponent`, one component per surface or preview layer. No
static object has a Tick. The preview draw of the whole plan is a dozen components.

### Data-driven rules
All tuning lives in `UCitixCitySettings` (a `UDeveloperSettings`). District rules, road class
dimensions and the plan-level dials (river width, band depth, lattice counts, arterial strides,
bridge count, jitter, district rotation, preview colours/heights) are data.

### Zero authored content (so far)
The game runs with **no project assets**: engine primitives (`/Engine/BasicShapes`),
`BasicShapeMaterial` for lit surfaces and `EmissiveMeshMaterial` (unlit + additive) for
glowing ones, tinted through cached `UMaterialInstanceDynamic`s, plus Enhanced Input
actions/contexts created in C++. This unblocks systems work; the art pass replaces
`FCitixSurfaceLibrary` and the input setup later.

### Emissive surfaces
`FCitixSurfaceLibrary::IsEmissive` marks window bands, lamps and vehicle lights. Those
use the additive emissive base material, and `SetEmissiveBoost` scales all of them at
once — this is what the time-of-day system drives so windows glow at night.

## World environment (`ACitixTimeOfDay`)

A WorldSystem actor that owns the day/night cycle:
- **Sun** directional light arcs 06:00 (east) → 12:00 (overhead) → 18:00 (west), with
  colour shifting from warm orange at low elevation to near-white overhead.
- **Moon** directional light, enabled only at night, registered as a *secondary
  atmosphere sun* so the night sky scatters a little moonlight instead of going black.
- **Sky light** real-time capture so ambient follows the sky.
- **Height fog** density varies with daylight.
- **Post-process volume** (unbound) with an exposure bias that darkens at night, so
  auto exposure cannot fake a daylight look.
- **Emissive boost** for windows/lamps/lights, driven by sun elevation.

`-CitixHour=<0..24>` pins a time for reproducible screenshots; `Citix.Time` and
`Citix.TimePause` control it at runtime, and `[` / `]` scrub time while held (the
controls sit on the player controller so they work in a car and on foot).

## Street lighting (`ACitixStreetLightSystem`)

A city has thousands of lamps, so no lamp gets its own light. The generator records
every lamp head position, and a small pool of shadowless point lights (default 26) is
re-targeted at the nearest lamp heads — refreshed on a timer or when the player moves
far enough — with intensity fading in with darkness. This is what makes the night city
read as lit at a fixed cost.

## Vehicle

`UCitixVehicleMovementComponent` is a Chaos rigid-body car model:
- `UBoxComponent` chassis with the physics root (unscaled, so child meshes are not
  sheared) + cosmetic meshes as children; the component reads the pawn's root
  primitive, so it does not care which primitive type is used.
- Per-wheel line-traced suspension: spring + damper, clamped.
- Tire forces with a friction circle (longitudinal drive/brake, lateral grip),
  handbrake reduces rear lateral grip.
- Speed-sensitive steering, aero drag, downforce, angular-velocity clamp,
  auto-recovery when upside down, and a manual reset that raycasts to the ground.
- **Grip model**: lateral tyre stiffness plus a bounded *stability assist* (a lateral
  deceleration scaled by slip angle) that keeps the car tracking its nose. While the
  handbrake is held the assist is only **reduced** (`DriftSlideAssistScale`) rather than
  removed, and the rear tyres keep most of their lateral grip (`HandbrakeGripScale` 0.80), so
  a drift holds its line instead of carrying its inertia sideways.
- **Power drift**: the handbrake *with throttle* multiplies engine force (×2.6) and top speed
  (×1.25) and does **not** brake, so drifting adds speed. Handbrake *without* throttle stays a
  normal handbrake turn that scrubs speed.

This was chosen over `UChaosWheeledVehicleMovementComponent` because the latter needs
an authored skeletal mesh + physics asset + wheel bones. The interface (input setters,
wheel states, speed readouts) is stable, so swapping in Chaos Vehicles later is
contained to this component.

## Traffic

Agents are **plain structs** in a flat array; a fixed pool of kinematic
`ACitixTrafficVehicle` actors is only used for presentation. Simulation is limited to a
radius around the player and despawns beyond a larger radius, so cost stays roughly
constant as the city grows.

Each car is a **planned** driver, not a reactive one. Every frame it builds a
`FTrafficAhead` by walking its lane up to three edges forward (queried through a
per-frame `edge -> agents` bucket, so look-ahead never scans the whole population):

- the **nearest vehicle** in its lane, including across junctions, with its speed;
- the **sharpest upcoming corner** and the braking curve that reaches the corner speed
  (`TurnSpeed`) exactly at the turn;
- the **next junction's** distance, light phase, and whether the queue beyond it is
  backed up (in which case the car stops *before* the crossing rather than parking on it).

The car then takes the minimum of its free-flow speed (eased so a road-class change is
not a step), the corner speed, the junction limit, the speed-dependent following limit
and the player-avoidance limit.

Steering is deliberately simple, and it is what makes the rest work: **the car's heading follows
the direction its presented position is actually moving.** A car on a straight path can then
only ever have a straight heading, so "goes straight but spins" is impossible rather than
tuned away. The presented position is the lane line plus a lateral offset, and both parts are
eased as *world-space vectors* over a distance rather than a time, so the arc shape does not
change with speed.

A junction is a **path blend**, not a kink. When a car crosses a node the base line's direction
changes instantly, so the presented path eases from the extrapolated line it came from onto the
line it is now on (`BlendFromEdge`). The blend length is the larger of an angle-based floor and
a speed-based term, so the corner is always swept over enough distance for the body to follow
it within its yaw budget - whether the car crossed at 2 km/h and then accelerated, or arrived
fast. The yaw rate is scaled by speed, so a car that has stopped cannot pivot on the spot, and
the corner speed hold keeps it from accelerating out of the turn before the body has rotated.

Three rules keep cars apart without any physics:

- **Time-headway following.** The desired gap is `FollowDistance + Speed * Headway`, and the
  permitted speed is `sqrt(leadSpeed^2 + 2*deceleration*room)` — the leader's speed is matched,
  not just its position, which removes the accelerate/brake oscillation.
- **A hard anti-overlap clamp**, applied *after* the car has crossed, against the nearest car in
  its lane on the edge it ends the frame on. A per-edge distance clamp cannot see across a node,
  which is where two cars used to end up sharing the same few metres.
- **Junction entry clearance.** A car will not cross a node if a car already sits within the
  minimum gap of the entry point of the lane it is about to occupy.

Junction behaviour is deliberately conservative, because "stopping in the middle of a crossing"
is what a player notices:

- **The stop line only applies before the line.** `ToStopLine = JunctionDistance - StopMargin`;
  if it is not positive the car is already past it, so no stop is demanded for that junction. A
  car on a short block whose *next* junction has a wide stop margin used to compute a negative
  distance and brake to a halt the moment it entered the crossing.
- **A junction whose far side is queued is not entered.** A slow leader within
  `JunctionExitClearance` past the node stops the car at the stop line instead.
- **The corner speed includes the room available** (`CornerSpeed`): turning sharply onto a short
  block has to be taken slowly, since the body cannot rotate faster than `TurnRateDegrees`
  however the path is drawn. That same speed is held through the corner, so the car cannot
  accelerate away while it is still swinging round.
- **Cars get into their far-side lane before the junction**, on that edge's own lateral axis, so
  they are exactly on their lane as they cross and afterwards. Doing the lane change after the
  crossing left them drifting across the road inside the junction.

The corner a car takes is **chosen and cached when it enters an edge** (`NextEdgeIndex`), and the advance loop then *takes that corner* - it does not draw a fresh one. This is the single
most important line in the system: when the advance loop drew its own random edge, a car would
brake, hold its corner speed and plan its steering against one turn and then physically swing
through a different, sharper one at full speed.

Debug: `Citix.Traffic` logs per-car target speed, gap, leader speed, corner, junction state,
the **crab angle** (heading minus motion direction) and yaw rate, plus the tightest same-lane
spacing in the population; `Citix.Traffic debug 1` draws each nearby car's heading (yellow) and
motion direction (cyan) so crab is visible; `Citix.Traffic watch 1` (or `-CitixTrafficWatch`)
logs one car - preferring one with a corner coming up - a few times a second so a turn can be
judged as a trajectory. Measured over 113 samples: crab max **9.4°**, mean **0.17°**, yaw rate
0 above 90°/s; over 98 cars the tightest same-lane centre-to-centre spacing was **1211 cm**
(car length 500 cm).

Spawns are gated: a candidate must be out of view, `SpawnClearanceDistance` from every existing
car, and leave `SpawnLaneClearance` of free lane ahead. Among the candidates that pass, the
roomiest is used, which spreads traffic instead of clustering it. Rejection counts are logged
(`Citix.Traffic`) so a spawn-starved city is visible rather than mysterious.

## Performance strategy

- No per-object ticking for static content.
- One HISM per surface per chunk; bulk `AddInstances` (single tree rebuild).
- Collision only where it matters (ground, façades, poles, trunks). Roads and markings are
  visual-only; sidewalks and curbs block characters and dynamic bodies (so pedestrians and
  ragdolls rest on the pavement) but are invisible to the car and its suspension traces, so
  a 16 cm curb never stops the vehicle.
- Traffic simulation is distance-budgeted.
- Traffic lights/markings are instanced, not components.
- **Distance culling** per surface on the chunk HISM components: small dense detail
  (foliage, props, poles, lamps) culls at 200–600 m; facades, glass, roads and the lit
  window bands are never culled because they carry the skyline. Culling affects rendering
  only, not collision.
- **Population density** is a multiplier on an already-sized pool, so rush hour costs
  nothing extra to set up and never allocates in play.
- Assets: no textures/materials authored yet; the art pass must respect instance
  counts and material count.

## Vehicle fleet (`FCitixCarLibrary`)

Cars are assembled from primitives in **car space**: origin on the ground between the
wheels, +X forward, +Y right, Z up. This one convention makes placement identical
whether the car sits on the ground (traffic) or under a suspended physics chassis
(the player, whose visual root is offset down by the resting suspension height).

- Six types with distinct silhouettes; trucks and buses have asymmetric axle spacing.
- `bHighDetail` builds the full car (glass panels, bumpers, mirrors, individual
  lights, wheels). The low-detail variant keeps body + windscreen + wheels and adds
  merged light bars, cutting traffic draw calls substantially.
- Painted parts use `FCitixSurfaceLibrary::GetTintedMaterial`, which quantises and
  caches colours, so a whole fleet shares a handful of material instances.

## Drift smoke (`UCitixDriftSmokeComponent`)
A pooled, code-driven emitter so no FX asset is required. `UCitixVehicleMovementComponent`
reports per-wheel lateral slip, grounded state, throttle and handbrake; the smoke
component converts that into an emission rate and spawns puffs at the rear wheel world
positions. Puffs grow to a peak mid-life and shrink out, with drag, rise and tumble.
This is the single swap point for a Niagara system later.

## Engine audio (`UCitixEngineAudioComponent`)

The project has no audio assets, so the engine note is **synthesised in real time** by a
`USynthComponent` subclass: `OnGenerateAudio` emits a falling-amplitude harmonic stack whose
fundamental follows a faked gearbox (rpm climbs within a gear and drops at shift points),
plus intake noise under throttle and a brighter layer while boosting. Control values are
pushed in once per frame by `SetEngineState` and smoothed per audio block to avoid zipper
noise. This is the swap point for real engine samples.

## Buildings and city layout

**The plan.** `FCitixCityMap` is the city's master plan: an irregular development boundary, a
river with per-point widths, 12 districts, the road hierarchy as polylines, and a list of
buildable blocks. `FCitixMapGenerator` produces it from a table of hand-placed district sites
(`FSiteDesc`) — each carrying its name, type, position, street bearing, block pitch, jitter,
skip chance and landmark chance. The districts themselves are the Voronoi cells of those
sites clipped to the boundary, so borders are organic while the *layout stays authored*.

**Blocks.** Each district's street grid produces cells; the part of a cell inside the district
becomes a block, in the district's own frame. Cells the border cuts through are quartered and
the pieces kept. Buildings then inherit the block's bearing, which is what stops the city
looking axis-aligned.

**Archetypes.** `ECitixBuildingArchetype` (13 values) gives the skyline distinct
silhouettes rather than one extruded box: twisting towers (stacked, progressively
rotated segments), crown-opening slabs (a rectangular hole framed by legs/lintel),
stepped ziggurats, crowned towers with spires or masts, twin towers with a sky bridge,
cylindrical towers with domes, spired masts, courtyard blocks, apartment slabs with
balcony rows, shophouse rows and sawtooth warehouse sheds. Each district has a weight
table, so industrial areas get sheds while the financial core gets towers.

**Landmark placement.** Each block carries a landmark chance derived from its distance to its
district's core, so signature supertalls cluster at the centre of the financial district and
thin out toward the edges.

## Characters (`FCitixCharacterLibrary`)

Low-poly humanoids (torso, head, hair, two arms, two legs) built from primitives, with a
procedural walk cycle. Limbs are single boxes; the walk rotates each limb about its
joint by offsetting the mesh so the joint stays fixed — no extra pivot components. Used
by both the on-foot player and pedestrians, and driven by real movement speed.

The rig also carries a general **pose** system (`FCitixHumanoidPose` +
`FCitixHumanoidRig::ApplyPose`) used by the get-up animation:

- the **upper body bends about the hip pivot**, so a bend looks like a bend instead of the
  whole body tilting
- **arms inherit the torso bend** in both position and rotation; legs hang from the hips
- each limb rotates about its own shoulder/hip joint; `BodyOffset` shifts the whole body,
  which is how a crouch lowers without jointed limbs

## Pedestrians (`ACitixPedestrianSystem`)

The same player-centred, flat-array agent model as traffic, but agents walk the
**sidewalks**: the road graph's lateral offset is moved out to the pavement. Agents
turn at junctions with a straight preference, are hidden beyond 140 m and recycled
beyond 300 m. Presentation is a recycled pool of low-poly actor rigs whose walk phase
is advanced from distance travelled.

### Ragdolls and getting up

**Every** hit turns a pedestrian into a physics ragdoll; remaining health decides the
outcome.

Whoever is walking carries a **hitbox**: a 48 × 48 × 170 cm query-only box set to *overlap* the
physics-body channel (the player's car) and the pawn channel (the on-foot player), and to ignore
everything else. A real collision therefore knocks a person down, while the car passes through
and is never stopped or damaged by them, and traffic (the vehicle channel) ignores people
entirely. The box is disabled while a body is ragdolling — the limbs are the collision then —
and while a pedestrian is pooled out of sight. The car tests contact by sweeping last frame's
position to this one against those boxes (`HitPedestriansAlongSweep`), because at speed it can
cross a whole body in a single frame, and a radius bubble around the car either misses that or
has to be made so large that it catches people who were never touched.

`ACitixPedestrian` owns the whole sequence so the system only has to poll it:

```
Ragdoll(impulse, hit, Knockdown | Death)
   │
   ├─ Knockdown: simulate → settle → capture the pose → play the get-up → walk on
   └─ Death:      simulate → lie → shrink-fade → recycle
```

Two things are easy to get wrong here and both are load-bearing:

- **Constraint frames.** A `UPhysicsConstraintComponent` uses its own world transform as the
  joint frame, so each one is created *at the joint* (neck, shoulders, hips — and the hair
  to the head). Initialising them at the actor root instead lets the locked linear limits
  drag the limbs toward the root and the body comes apart. Hair is a separate box and needs
  its own joint or it just falls off.
- **The get-up must start from the physical pose.** When the physics settles, the body's pose
  is captured into the actor frame and frozen; the limbs are handed back to the actor and the
  authored animation blends out of that captured pose over its first third. Without this the
  body snaps from wherever it landed into a canned pose.
- **Collision channel.** Pavements and kerbs block `Pawn` and `WorldDynamic` but deliberately
  **ignore** the physics-body channel, because the player's car is itself a physics body and a
  16 cm curb must never stop it. Ragdoll limbs therefore collide as `WorldDynamic`, not as a
  physics body: as a physics body they fell straight *through* the slab they landed on, so a
  knock-down sank into the pavement and the pavement hid most of the body.

Victims are collected before any of them are ragdolled. Releasing a ragdoll under budget
pressure reshuffles the agent array, and mutating it mid-iteration used to leave the hit loop
writing to whichever agent had been swapped into the slot.

On recovery the agent's `Distance` and `LaneOffset` are re-projected from where the body
actually came to rest, so the walker carries on from there rather than teleporting back onto
the pavement. `MaxConcurrentRagdolls` (14) bounds the physics cost; when it is full the
oldest knock-down is made to stand up early (never deleted).

## Player mode switching

`ACitixDrivingPlayerController` owns every mode transition and the sandbox verbs, so the
pawns stay focused on their own behaviour:

- **Exit (F in car):** spawn `ACitixOnFootPawn` at a computed exit transform beside the
  car, park the car (zero inputs + handbrake), then `Possess` the character.
- **Enter (F on foot):** enter the player's own parked car if one is in range; otherwise
  **take over any parked or moving traffic car** — the player's vehicle pawn is moved to
  that car's pose, adopts its body type and paint (rebuilding the visual car *and*
  re-configuring the physics wheels, since the wheelbase differs per type), and the source
  car is released from the traffic system.
- **V** cycles the player's car type.
- **Photo mode (P):** spawns a free `ACameraActor`, `UnPossess`es the pawn (which also
  zeroes throttle/steer, so the car coasts rather than driving on), pauses the clock and
  hides the HUD.
- **M / G / J** drive the map, the waypoint cycle and delivery jobs via the sandbox
  director.

Each pawn adds its Enhanced Input mapping context on possession and removes it on
`UnPossessed`, so driving, on-foot and photo bindings never fight. The controller's own
contexts (time scrub, sandbox verbs, photo camera) are added at priority −1 and are
gated by state inside their handlers.

## Sandbox layer (`ACitixSandboxDirector`)

One actor owns everything that turns driving into play. It is deliberately cheap: a 4 Hz
tick for discovery/jobs/routing/density plus a per-frame air-time check, and no per-frame
work proportional to city size.

- **Points of interest** are built once from the district cores, the recorded landmark
  towers and every eighth junction; driving within `DiscoveryRadius` marks one discovered.
- **Navigation** cycles the nearest few POIs so goals are always reachable, and the route
  is a real **Dijkstra path over the road graph**. At ~100 nodes a linear-scan Dijkstra is
  cheaper than a heap, and it runs at the low update rate anyway.
- **Delivery jobs** pick a road node in a distance band, size the timer from an assumed
  cruise speed, and spawn an emissive beacon pillar at the destination.
- **Stunt scoring** accumulates air time between the movement component's grounded
  transitions, but only while the car is actually moving (otherwise settling onto the
  ground at spawn counts as a stunt).
- **Density** maps the hour to a multiplier through a keyframed curve and pushes it to the
  traffic and pedestrian systems. Their pools stay sized for the peak, so density changes
  never allocate during play.

The director is configured by the game mode with a **copy** of the road network, so it is
independent of the generator's lifetime and can be regenerated with the city.

## Weather (`ACitixWeatherSystem`)

- Rain is **one instanced static mesh component** (~220 streaks) — a single draw call,
  rather than a particle system per shower.
- The streak volume **follows the player and wraps horizontally**, so a fixed pool covers
  unlimited distance while driving; only transform writes happen per frame.
- Rain drives the look through `ACitixTimeOfDay::SetRainIntensity`: road wetness, fog
  density, ambient light and emissive haze all move together. Particle-only rain reads as
  sparkles; this reads as weather.
- A four-state cycle (clear → starting → raining → stopping) with night-biased odds, plus
  `Citix.Rain` and `-CitixRain` for tests.

## Development tooling (headless-friendly)

Because the game is fully code-driven, it can be verified without manual play:

| Command-line switch | Behaviour |
|---|---|
| `-CitixSelfTest` | Generate the city, log instance/component stats, exit |
| `-CitixScreenshot` | Generate, then render an overview of the whole city to `Saved/Screenshots` |
| `-CitixScreenshot -CitixScreenshotChaseOnly` | Street-level shot from the player camera |
| `-CitixScreenshot ... -CitixDriveTest` | Applies full throttle, logs speed/grounded, screenshots |
| `-CitixScreenshot ... -CitixDriftTest` | Throttle + steering + handbrake, logs slip and smoke puffs |
| `-CitixScreenshot ... -CitixOnFootTest` | Exits the vehicle first, screenshots the on-foot TPS view |
| `-CitixScreenshot ... -CitixWalkTest` | Exits, then injects a real `W` key press and logs camera-forward alignment |
| `-CitixScreenshot ... -CitixCornerTest` | Steady throttle + fixed steering; logs the body slip angle |
| `-CitixScreenshot ... -CitixReenterTest` | Exit → re-enter → throttle; verifies the car still drives |
| `-CitixScreenshot ... -CitixCarInspect` | Close three-quarter camera on the player car (wheel/model inspection) |
| `-CitixScreenshot ... -CitixSandboxTest` | Cycles a waypoint, starts a delivery job, swaps the car, opens the map, screenshots |
| `-CitixScreenshot ... -CitixWaterView` | Camera over the river looking downstream (water inspection) |
| `-CitixScreenshot ... -CitixRagdollTest` | Knocks down nearby pedestrians; logs ragdolls/recoveries, follows one body with a camera |
| `-CitixScreenshot ... -CitixRagdollTest -CitixKillTest` | Same, but lethal: verifies the bodies fade rather than stand up |
| `-CitixCaptureAt=<seconds>` | Override the screenshot time (capture any stage of the ragdoll/get-up) |
| `-CitixRain` | Force a downpour at startup (for weather screenshots) |
| `-CitixNoTraffic` | Disable traffic (isolate vehicle physics in tests) |
| `-CitixRoadGraph=<0..4>` | Draw the road graph overlay at startup (see below) |

Any screenshot run accepts `-CitixHour=<0..24>` to pin the time of day.
Console commands: `Citix.Regenerate`, `Citix.Clear`, `Citix.Stats`, `Citix.Time`,
`Citix.TimePause`, `Citix.Rain <0..1|auto>`, `Citix.Traffic [debug 0|1]`, `Citix.RoadGraph <0..4>`.
`ACitixCityGenerator::GenerateCity` is also `CallInEditor`, so the city can be
regenerated from the actor's Details panel.

### Road graph overlay (`ACitixRoadGraphDebug`)

The graph agents walk on is invisible while playing — they reference edge indices and lane
offsets, never geometry — which makes traffic behaviour hard to reason about. This draws it
in world space, instanced (about a dozen draw calls, no per-frame cost), rebuilt only when
the mode changes or the graph is regenerated:

| Mode | Draws |
|---|---|
| 1 | Edges coloured by road class (wider for majors), a direction chevron per edge, and a node marker per node whose size encodes its degree |
| 2 | 1 + lane centrelines for both directions — the exact geometry the traffic drives on |
| 3 | 2 + node labels (`N<index>  deg <n>`) |
| 4 | 3 + edge labels (`E<index> <class> <length>`) — the heaviest mode |

Nodes are coloured by degree: **red and large = degree 1 (a dead end)**, grey-blue = 2 (a
bend), orange = 3 (a T-junction), violet = 4+ (a crossing). Bridges are magenta, closed stubs
dark red. The build also logs the degree histogram, which is the number that decides whether
an agent can ever be left with nowhere to go but back the way it came (this map: 0 dead ends).

Turn it on with `Citix.RoadGraph 2` (works in PIE, which is what F8 leaves you in), by ticking
**Road Graph Debug** on `ACitixCityGenerator`, or with `-CitixRoadGraph=2` at launch. Labels
are sized for the overview camera, so they are large up close; modes 1–2 are the street-level
views.

Note: the overlay is instanced meshes, not `DrawDebugLine`. Lines added to the engine line
batcher (a translucent-pass primitive) do not render in this project's `-game` runs, and
`DrawDebugPoint` renders as screen-relative billboards that swamp the view; instanced slabs
are deterministic, occlude correctly, and show up in the editor viewport too.

## Extension points

| Area | Where to extend |
|---|---|
| Real building meshes | `FCitixSurfaceLibrary` mesh/material mapping |
| **Water look / waves** | `M_CitixWater` in `CitixMaterialSetupCommandlet.cpp`. The wave table (directions, wavelengths, slopes, layer) and the macro wave table are the two places to change structure; the exposed scalar/vector parameters cover tuning |
| **Water flow direction** | `FCitixBoxInstance::Flow` → `ACitixCityChunk` custom data → material |
| Water colour / gloss | `FCitixSurfaceLibrary::GetColor` / `GetRoughness` for `Water` and `WaterDeep` (fed into `WaterColor`, `RoughnessMin/Max/AtGrazing`) |
| **City layout / districts** | `DistrictSites` table in `City/CitixCityMap.cpp` (one line per quarter) |
| **River and boundary** | `FCitixMapGenerator::Generate` (river control points, boundary lobes) |
| **Road hierarchy** | `FCitixMapRoad::Class` at generation + `RoadRules` in settings |
| **Block sizes / street angles** | the per-district `FSiteDesc` fields |
| New building archetypes | `ECitixBuildingArchetype` + the switch in `FCitixBuildingGenerator` |
| District look | `FCitixBuildingGenerator::PickArchetype` weight tables + `PickBandSurface` |
| Day/night look | `ACitixTimeOfDay` (sun arc, exposure, emissive boost) |
| Glowing materials | `FCitixSurfaceLibrary::IsEmissive` / `SetEmissiveBoost` |
| Block patterns / lots | `ACitixCityGenerator::PickBlockPattern` / `EmitLot` / `GenerateBlock(Block)` |
| Real vehicle meshes | `FCitixCarLibrary::BuildPartDescs` (one place per type) |
| **Car performance / drift character** | `FCitixCarLibrary::GetPerformance` (one block per car type) |
| **Suspension from mass** | `SuspensionStiffnessPerKg` / `SuspensionDampingPerKg` / `MaxSuspensionForcePerKg` on the movement component |
| **Crash damage curve** | `CrashSpeedThreshold`, `CrashDamagePerSpeed`, `CrashDamageMax` |
| New vehicle types | add to `ECitixCarType` + `BuildPartDescs` + `GetPerformance` |
| Pedestrian behaviour | `ACitixPedestrianSystem` (avoidance, crossings) |
| Game rules / progression | `ACitixSandboxDirector` (discovery, jobs, stunts, density) |
| Navigation routing | `ACitixSandboxDirector::RebuildRoute` (swap Dijkstra for A*/navmesh) |
| Weather | `ACitixWeatherSystem` (add fog banks, storms, snow as new states) |
| Rain look | `ACitixWeatherSystem` streak scale/speed + `ACitixTimeOfDay::SetRainIntensity` |
| HUD screens | `ACitixDrivingHUD::DrawFullMap` / `DrawSandboxPanels` |
| Elevated roads | `FCitixMapRoad` (add a Z / level field) + `GenerateRoadEdge` |
| Concave coastline | `FCitixMapGenerator` boundary + a non-convex clip path |
| Real smoke FX | `UCitixDriftSmokeComponent` (assign a Niagara system there) |
| Real engine/vehicle audio | `UCitixEngineAudioComponent` (swap the synth for samples) |
| On-foot gameplay | `ACitixOnFootPawn` (ragdoll, animation, interaction) |
| District behaviour | `FCitixDistrictRule` in settings + `FCitixCityMap::GetDistrictTypeAt` |
| New road types | `ECitixRoadClass` + settings `RoadRules` |
| World Partition | Convert `ACitixCityChunk` actors into WP cells / level instances |
| PCG | Feed the road graph / lot data into PCG graphs |
| Chaos Vehicles | Replace `UCitixVehicleMovementComponent` internals |
| Traffic AI detail | `ACitixTrafficSystem` (lane change, intersection reservation) |
