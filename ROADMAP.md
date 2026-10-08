# Citix — Roadmap

Milestones, not micro-tasks. Each milestone ends with a self-review and a decision on
whether the city can grow.

## M0 — Foundation ✅ (session 1)
- Module structure, core types, data-driven settings.
- Road network, city generator, chunks, instanced geometry pipeline.
- Building + prop generators with district rules.
- Player vehicle, driving game mode, HUD, runtime input.
- Basic traffic system.
- Project documentation.

## M1R — CITY GENERATION RESTART 🔁 (session 10, IN PROGRESS)
The first city attempt was a uniform grid, then an over-corrected "random non-grid" that
produced overlapping roads, buildings intersecting roads and each other, messy intersections
and diagonally-crossing bridges. It was **deleted, not repaired**.

Replacement principle: **structured irregularity** — orderly topology, geography-driven
geometry, and a plan that validates itself.

- [x] **Delete the old city generation** (plan, roads, blocks, buildings, props, water)
- [x] Handbrake is a brake: no acceleration while Space is held
- [x] **Phase 1 — Macro plan**: river, districts, arterials, bridges, secondary roads, blocks
- [x] Self-validating plan (0 crossings, 0 duplicates, 0 degenerate cells, all angles 60-120°)
- [x] Planning preview with colour legend and district labels
- [x] **Plan approved**
- [x] Phase 2 — Parcels: subdivide blocks along their own orientation, emit level-3 local streets
- [x] Phase 2 — Buildings: fill parcels by density zone, towers grouped in clusters
- [x] Phase 2 — Landmarks inside the primary cluster only
- [x] Phase 2 — Detail: street furniture, lamps, trees by road class and district
- [x] Phase 2 — Traffic, pedestrians and street lighting running on the new graph
- [x] **Water: rebuilt as ten waves in three layers with dispersion-driven speeds, a macro breakup field, fresnel roughness and depth colour — no textures, no tiling**
- [x] **Per-car-type performance: mass, top speed, power, hull and drift character (six profiles)**
- [x] **Drifting restored: the handbrake cuts acceleration and braking, not the slide**
- [x] **Hull strength ×4 and no damage below 30 km/h**
- [ ] Art pass: façade/window materials, star field, real character meshes
- [ ] Decide the diagonal question (see CURRENT_STATE limitation 1)
- [ ] Add mid-block local streets to the routing graph
- [ ] Phase 3 — Elevation: flyovers, tunnels, grade-separated diagonals
- [ ] Phase 4 — Scale to 2 km², then World Partition + HLOD

### Open design question
Diagonal streets and the 60-120° intersection rule are mutually exclusive for at-level
crossings (a line crossing an orthogonal grid always makes an angle below 60°). Phase 2 needs
a decision: accept Broadway-style 45° junctions, or make diagonals grade-separated once there
is elevation, or give whole districts their own bearing on a much finer lattice.

## M1 — 1 km² vertical slice (superseded by M1R)
Goal: a playable, believable 1 km² city that proves the architecture.
The systems below were built and are retained; the *city layout* was replaced.

- [x] Architecture + generation pipeline
- [x] Player vehicle (physics car, cameras, reset)
- [x] Basic traffic with lights
- [x] World bootstrap + HUD
- [x] **Runtime verification and tuning** (editor build + headless play tests)
- [x] **Low-poly vehicle fleet with real collision (6 types)**
- [x] **Drift smoke particle emitter**
- [x] **On-foot third-person character with enter/exit**
- [x] **Ambient pedestrians on the sidewalks**
- [x] **13 building archetypes (towers, twisted, crown-opening, ziggurat, twin, cylinder, spire, courtyard, slab, shophouse, warehouse)**
- [x] **6 block layout patterns + landmark tower clustering**
- [x] **Fixed on-foot movement direction**
- [x] **Time of day (sun, moon, sky, fog, exposure, glowing windows/lamps/lights)**
- [x] **Player-centred street lighting pool (lit night city)**
- [x] **`[` / `]` time controls**
- [x] **Traffic turn/lane-change smoothing; handbrake-only smoke; steering feel; character grounding**
- [x] **Ragdoll knock-downs, citizen health, crash damage caps and explosion**
- [x] **Night lighting pass: district profiles, window variants, accents, pooled lights, star sky, wet streets**
- [x] **Pooling gated by real line of sight (no spawning/hiding/recycling in view)**
- [x] **Discovery / points of interest + score**
- [x] **Delivery jobs (destination beacon, timer, reward)**
- [x] **Stunt scoring (air time + distance)**
- [x] **Full city map (M) with GPS route, waypoints and POI overlay**
- [x] **Enter any vehicle (parked + traffic) and cycle the player car type (V)**
- [x] **Photo mode (P)**
- [x] **Rain weather system driving wetness, fog, ambient and haze**
- [x] **Time-of-day traffic/pedestrian density (rush hour vs dead of night)**
- [x] **Pedestrian kerb pauses at junctions**
- [x] **Distance culling for props/foliage on chunk instances**
- [x] **Procedural engine audio (no assets required)**
- [x] **Parked cars are no longer wrecked by being rear-ended**
- [x] **Physics ragdolls for every knock-down (constraint frames fixed, hair jointed)**
- [x] **Articulated get-up animation (hip-pivot pose system, pose captured from physics)**
- [x] **Drift re-tuned: forward drive instead of sideways slide**
- [x] **City master plan phase: river, Voronoi districts, irregular boundary**
- [x] **Non-grid street network: ring roads, radial avenues, per-district street bearings**
- [x] **Six road classes (Alley → Local → Collector → Arterial → Boulevard → Ring Highway)**
- [x] **Four new districts (Parkland, Waterfront, University, Market) with their own rules**
- [x] **Bridges, embankments and riverside promenades**
- [x] **Plan welded into a routable graph (intersections, T-junction snapping, junction trims, connectivity check)**
- [x] **Road surfaces trimmed at every junction (no coplanar road tops, no depth conflict)**
- [x] **Waterfront fixed: riverside road set back from the water, promenade measured per bank, bridge crossings kept clear**
- [x] **Traffic AI: cross-junction look-ahead, speed-dependent time headway, hard anti-overlap clamp, corner-speed hold, path-following steering**
- [x] **Traffic spawning: clearance + lane-space + out-of-view gates, roomiest-candidate bias, local density cap**
- [x] **Ragdolls rest on the surface they land on (dynamic-body collision vs pavements/roads), no more sinking out of sight**
- [x] **`Citix.Traffic` diagnostics + debug draw (target speed, gap, leader, corner, junction, tightest spacing)**
- [x] **Traffic steering rebuilt: heading follows the presented motion, so a straight path cannot spin; junction path blend + speed-scaled yaw + committed-corner driving (crab max 9.4°, mean 0.17°)**
- [x] **`-CitixTrafficWatch` / `Citix.Traffic watch 1`: log one car's crab angle and yaw rate over time**
- [x] **Traffic junction rules: stop line only before the line (no stopping inside a crossing), entry clearance, corner speed limited by the room available, get-in-lane before the junction**
- [x] **Pedestrian hitboxes: query-only body volume, reacts to the player's car and on-foot collision, swept contact test in the car**
- [x] **Parked cars disabled (`MaxParkedVehicles = 0`) at the user's request; the system is intact and re-enables with one value**
- [x] **Road graph overlay: `Citix.RoadGraph 0-4` / `-CitixRoadGraph=` draws edges by class, nodes by degree, lane centrelines and labels (instanced, no per-frame cost)**
- [ ] Art pass: façade/window materials, night star field, proper smoke sprite/Niagara
- [ ] Author project map `L_Citix` (World Partition) + `CitixSetup` commandlet
- [ ] Traffic lane changing/overtaking; intersection reservation; two-way collision
- [ ] Pedestrian avoidance (avoid each other and the player) and real crossings
- [ ] Audio: sampled engine/tyre, city ambience, rain (procedural engine placeholder exists)
- [ ] Elevated roads: lift the ring highway and bridges onto structures, add interchanges
- [ ] Concave coastline / bays so the boundary stops being a convex ring
- [ ] Move traffic/pedestrian presentation to pooled instancing
- [ ] Verify performance at 2 km before scaling

**Exit criteria:** stable 60+ FPS on mid hardware with ~1 km² of dense city, traffic,
pedestrians, and no generation errors.

## M2 — City quality pass
- Material/art pass: façades with windows, roads with markings/wet look, emissive
  night windows, sidewalk materials.
- Street-level detail: signage, bus stops, crossings, guardrails, fences, planters.
- Environment: time of day, sky, fog, weather, street lighting at night.
- Audio: vehicle engine, tyre, traffic ambience, city ambience.
- Improved vehicle handling + multiple vehicle types.

## M3 — Scale to 4 km²
- World Partition cells + data layers; streaming of chunks.
- HLOD setup for chunked geometry.
- District streaming and generation only when needed.
- Scale traffic budget and verify constant-cost simulation.
- Compressed district layout so the 4 km² reads as multiple distinct areas.

## M4 — Living city
- Pedestrians (Mass Entity / Crowd) in dense districts.
- Traffic lane changing, intersections, parking.
- Elevated highways, tunnels, bridges.
- Parked vehicles, roadside activity, dynamic city ambience.

## M5 — 8–15 km city
- Multi-district megacity with ring roads and major highways.
- Fast travel? (TBD, driving-first).
- Final performance tuning, HLOD iteration, memory budget.

## Cross-cutting (ongoing)
- Keep documentation current (`CURRENT_STATE.md` every session).
- Keep everything data-driven and instanced.
- Add automation tests for generators as they stabilise.
- Remove legacy template code/assets once nothing references them.
