# Citix — Player Controls & Baseline Test Flow

Recorded from `Source/Citix/Player/*` and `Sandbox/CitixSandboxDirector.*`.
Seed 1337 deterministic: same city, same landmarks, every run.

## Controls

| Key / Button | Context | Action |
|---|---|---|
| W A S D / Left stick | Driving | Throttle / steering |
| Space / B | Driving | Handbrake (drift; cuts drive while held) |
| Shift / LT | Driving | Boost |
| Left mouse | Chase mode, chaser driving | Ice Wave: 40 m / 120°, two-charge reserve, +1 per 90 s; runner loses 30% current speed and cannot accelerate or boost for 3 s |
| R | Driving | Reset vehicle |
| C | Driving | Toggle chase / hood camera |
| F | Driving (slow) | Exit vehicle |
| W A S D / Left stick | On foot | Move |
| Mouse / Right stick | On foot, driving | Look |
| Space / B | On foot | Jump |
| Shift / L-shoulder | On foot | Sprint |
| F | On foot (near car) | Enter vehicle (own, parked, or traffic) |
| M | Any | Full city map |
| G | Any | Cycle waypoint through nearest POIs |
| J | Any | Start delivery job |
| E / Gamepad X | On foot, near POI | Start contextual activity (tour near landmarks, else route) |
| E | On foot, near gunsmith | Talk: open the buy UI (click rows + BUY / LEAVE buttons, or 1-5 + E, X / ESC leave) |
| X / Gamepad Y | Any | Close shop → cancel activity → delivery → map waypoint (first active wins) |
| Left mouse / RT | On foot | Fire current gun (buy one first - orange markers) |
| Right mouse (hold) | On foot | Aim down sights (body follows camera, sniper shows scope) |
| T / D-pad up | On foot | Reload (also auto-reloads on empty mag) |
| Y | Any | Join / leave the competitive race (2+ drivers, ordered checkpoints) |
| P | Any | Photo mode (free camera, HUD hidden) |
| WASD + Q/E + Mouse | Photo mode | Move / rise-fall / look |
| ENTER / Start | Photo mode | Capture photo (scores the view, saves a shot) |
| V | Driving | Swap car type (keeps hull fraction) |
| `[` / `]` or `,` / `.` | Any | Scrub time of day |

Prompts never block driving: E / F prompts appear only when actionable.

## Core loop

Explore → discover landmarks/districts → E starts an activity → follow the green
route line → arrive for score → run deliveries for funds → buy guns/ammo at orange
gunsmith markers → Y races rivals through checkpoints → optional trouble (gunfire,
kills, crashes) → evade police (stars decay when far and quiet) → back to
exploration. One priority contract per in-game day pays 3x (press J when offered).
Talking, activities and guns are on-foot only; the shop UI takes the mouse and
freezes movement until X / ESC. Death costs nothing but strands your job (failed).

## Baseline test flow (manual, ~5 minutes)

1. Play, drive forward 10 s: speed, minimap, `DISCOVERED x/y` and `SCORE` update.
2. Drive toward an undiscovered dot: landmark card appears (name + flavour + x/y).
3. Cross a district boundary: district card appears once per district.
4. Near a POI, press **E**: activity starts (objective panel upper-left, beacon).
5. Follow the green route line to the beacon: arrival toast + score increases once.
6. Press **E** again with no POI nearby: nothing starts (no prompt shown).
7. Start an activity, press **X**: cancelled cleanly, beacon gone, no reward.
8. Drive away from the route: `OFF ROUTE` hint after ~6 s, no failure.
9. Press **P**: HUD hides, hint line shows. **ENTER**: photo scored + toast.
10. Run a delivery (**J**): completion pays funds + score. After midnight in-game,
    a priority contract is offered (3x pay, tighter clock).
11. Find an orange gunsmith marker, walk/drive to the employee, press **E**: buy a
    gun with delivery earnings (`1-5` select, `E` buy, `X` leave). Ammo is limited;
    `T` reloads. Hold **RMB** to aim (sniper shows a scope zoomed in).
12. Fire near traffic/peds: they stop/flee, heat rises. Kills (+35 each) bring
    police fast: keep driving cleanly far from them and stars decay (`All clear`);
    let a unit catch you (slow, close, 3 s): busted → relocated, score −100.
13. Press **M**: map shows POIs, shops, route, legend. **X** clears the waypoint.

## Headless verification

Build `CitixEditor Win64 Development`, then from the project root:

```
UnrealEditor-Cmd.exe Citix.uproject <map> -game -nullrhi -log -CitixSelfTest
... -CitixScreenshot -CitixScreenshotChaseOnly -CitixSandboxTest
... -CitixScreenshot -CitixActivityTest
... -CitixScreenshot -CitixWeaponTest
... -CitixScreenshot -CitixWantedTest
```

Each gameplay test prints `PASS` / `FAIL` lines with the measured values.
Seed check: `-CitixSelfTest` must report the same district/road/block counts as
`CURRENT_STATE.md` (seed 1337: 8 districts, 44 roads, 4 bridges, 130 blocks).
