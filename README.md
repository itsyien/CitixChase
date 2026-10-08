# CitixChase

Two-player city pursuit game built from the Citix driving and procedural-city foundation. See [the two-PC play guide](Docs/PLAY_CITIX_CHASE.md) for host/join setup and the round rules.

An open-world driving and exploration game set in a fictional, Shanghai-inspired
East Asian megacity, built in Unreal Engine 5.

The focus is **driving, exploration and the feeling of a huge living city** — not
combat. The city is **procedurally generated from data-driven rules**, built for
scale (ultimately a 10–15 km wide city) with instanced, chunked geometry and
player-centred simulation.

## Documentation

| File | Purpose |
|---|---|
| [PROJECT_GOAL.md](PROJECT_GOAL.md) | Vision, design pillars, districts, target scale |
| [MAIN_STRUCTURE.md](MAIN_STRUCTURE.md) | The chase game's design contract and where it lives in the code |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Module layout, systems, decisions, extension points |
| [CURRENT_STATE.md](CURRENT_STATE.md) | What works, known issues, next actions |
| [ROADMAP.md](ROADMAP.md) | Milestones M0–M5 |

## Status

Milestone 1 (1 km² vertical slice) is **functional and verified**: the city generates
in ~14 ms, the car drives, you can step out and walk, traffic and pedestrians populate
the streets, drift smoke works, and the slice renders at ~110 fps. The next major step
is the art/material pass.

CitixChase (the two-player city pursuit layer) builds and its standalone Development
executable now boots past static initialization into the city — see
[MAIN_STRUCTURE.md](MAIN_STRUCTURE.md) and the
[progress handout](Docs/CITIXCHASE_PROGRESS_HANDOUT.md).

### Feature highlights

- **Drive** a physics-based car (or a van/truck/bus in traffic) with drift smoke.
- **Get out and walk** — third-person low-poly character with sprint and jump.
- **Living streets** — ~150 traffic vehicles obeying lights, ~60 pedestrians on the
  pavements.
- **Procedural city** — districts, roads, blocks, buildings, props, all data-driven.
- **A real city plan, not a grid** — the layout is planned before it is built, on a
  river-relative lattice: a river spine with perpendicular bridges, quarters whose borders
  follow the water and the arterials, a three-level road hierarchy, and an irregular growth
  edge. The plan validates itself (zero accidental crossings, every intersection within
  60–120°) and is drawn as a coloured planning preview.

> **Current phase:** Phase 2 complete. The city is built on the approved macro plan — river,
> bridges, a three-level road hierarchy, 130 blocks, 699 parcels, 564 buildings, landmark
> clusters, street furniture, traffic, pedestrians and night lighting. Tick *Show Planning
> Preview* on `ACitixCityGenerator` to see the underlying plan.
- **Varied skyline** — 13 building archetypes (twisting towers, crown-opening slabs,
  stepped ziggurats, twin towers, cylinder towers, spired masts, courtyard blocks,
  apartment slabs, shophouse rows, warehouses) across 6 block layout patterns, with
  landmark supertalls clustering around the financial core.
- **Day / night cycle** — the sun arcs, the moon and sky follow, and windows, street
  lamps and vehicle lights glow after dark.
- **Game layer** — points of interest to discover, delivery jobs with a timer and reward,
  stunts scored on landing, and a score that tracks them all.
- **Six car types that actually drive differently** — a hatchback, sedan, pickup, van, truck and
  bus each have their own mass, power, top speed, hull strength and drift character, and you can
  drive any of them.
- **Living water** — the river is ten layered waves with real dispersion-driven speeds, a
  large-scale breakup field that makes some patches choppier, glossier or darker than others,
  fresnel-driven roughness and a depth tint. Reflections come from Lumen. No textures, no tiling,
  no extra passes.
- **Navigation** — a full city map (**M**), cyclable waypoints (**G**) and a GPS route
  drawn through the actual road network.
- **Enter any car** — step out, then take over any parked or moving vehicle (**F**), or
  cycle your own car's body type on the spot (**V**).
- **Weather** — a rain cycle with a downpour that genuinely changes the city's look, and a
  population that follows the clock (rush hour vs dead of night).
- **Photo mode** (**P**) — a free camera with the clock paused and no interface.
- **Procedural engine audio** — the car has a voice even though the project ships with no
  audio assets.

## Quick start

1. Open `Citix.uproject` in Unreal Engine 5.8.
2. Press **Play**. The game mode bootstraps the city, lighting and spawn
   automatically — no manual setup required.

### Controls

| Input | Action |
|---|---|
| `W` / `S` or arrows | Throttle / brake-reverse |
| `A` / `D` | Steer |
| Mouse | Look around (returns behind the car) |
| `Space` | Handbrake |
| `R` | Reset / recover vehicle |
| `C` | Toggle chase / hood camera |
| `F` | Exit the car / enter the nearest car (any parked or traffic car) |
| `V` | Cycle the player car's body type |
| `M` | Full city map |
| `G` | Next navigation waypoint |
| `J` | Start a delivery job |
| `P` | Photo mode (free camera, clock paused) |
| `[` / `]` (or `,` / `.`) | Scrub time of day (hold) |
| `~` | Console (`Citix.Regenerate`, `Citix.Time 21`, `Citix.TimePause 1`, `Citix.Rain 1`, `Citix.Stats`) |

### On foot

| Input | Action |
|---|---|
| `W` `A` `S` `D` | Walk |
| `Shift` | Sprint |
| `Space` | Jump |
| Mouse | Look |
| `F` | Enter the nearest vehicle (your own car, or any parked/traffic car) |

### Build (command line)

```
Engine\Build\BatchFiles\Build.bat CitixEditor Win64 Development ^
  -Project="<path>\Citix.uproject" -WaitMutex
```

> The Unreal Editor must be closed when building (Live Coding locks the module DLL).

### Verify without playing

```
Engine\Binaries\Win64\UnrealEditor.exe "<path>\Citix.uproject" ^
  -game -nullrhi -log -stdout -CitixSelfTest -unattended -nosplash
```

See `CURRENT_STATE.md` for the full list of verification switches.

## Tuning the city

All city parameters live in **Project Settings > Game > Citix City**
(`UCitixCitySettings`): city size, block spacing, chunk size, road dimensions per
class, per-district building rules, furniture density, traffic budget. No code
changes are needed to retune the city.
