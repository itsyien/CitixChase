> Superseded tracker presentation: see GROUND_RING_AND_RELAY_FX.md. The bush distance changes below are still current.

# Camera-relative runner tracker and foliage ranges

2026-10-07, v1.0 update.

## Inspection and direction

The prior indicator was an eight-segment emissive ring plus cone attached to every vehicle. It used car-local presentation and flattened the runner direction to XY, which made its size/placement depend on the world camera and discarded elevation. Tracking was restricted to the local chaser while the server revealed the runner. The existing Canvas chase HUD already uses a consistent 1280x720 reference scale, muted panels, thin vector shapes and the replicated PlayerArray/pawn relationship.

The refinement uses that HUD and existing target data instead of a new widget framework, networking feature or replicated actor. It removes ten tracker scene/mesh components from each car, including the root, and replaces the redundant red runner bracket marker with one coherent tracker. Capture prompts and other objective markers remain.

## Player behavior

A 56-pixel segmented ring sits in the upper-right HUD, clear of the player vehicle, role panel and timer. The pale cyan chevron is primary; the ring is darker and thinner. A small RUNNER label and 3D distance in metres sit above/below it. Vertical offsets beyond four metres show an UP/DOWN caret and height difference. Camera yaw determines heading, not the car's heading; shortest-arc exponential interpolation settles a 90-degree turn within about 0.1 seconds at 30/60/120 Hz. First acquisition/replacement snaps to the new target.

A genuinely visible runner inside a safe screen area receives a small diamond and RUNNER distance label. The compass becomes quieter as the marker fades in. A visibility query runs at 10 Hz; no marker is shown outside the safe area or when that query reports obstruction. The player vehicle and existing HUD panels are reserved regions. The original server reveal window, role and pursuit gates remain authoritative: no runner information appears when unrevealed.

Proximity changes a small opacity pulse, never a large spin/scale animation. Dark outlines and translucent label backing retain contrast on bright backgrounds; the same styling remains readable at night and with bloom disabled. The new presentation is camera-relative and uses the HUD's reference-resolution scaling.

## Bush ranges

| Preset | Start distance | End cutoff |
|---|---:|---:|
| Low | 8 m | 12 m |
| Medium | 90 m | 160 m |
| High | 250 m | 400 m |
| Max | 450 m | 650 m |

Existing city components update immediately when selecting graphics presets, including returning to Low. The HISM distance values compensate for the engine's global view-distance multiplier. Per-component LOD-distance scaling prevents the small authored mesh from being culled prematurely by the HISM screen-size limit. The actual render data bounds were measured at 58.79 cm with foliage.MinimumScreenSize=0.000005. The bush mesh remains 204 triangles, no dynamic shadows, query-only drag collision, and no per-bush actor/tick. These are rendering distance cutoffs; the authored bush has one mesh LOD.

## Verification and review

Tracker-Red.log records failures for camera bearing, shortest-arc rotation, reveal gates, removal of old car geometry and preset range updates before implementation. Tracker-FinalSuite.log passes all 24 tests, exit 0, including host-travel mesh lifetime, movement, collision, Ice Wave, graphics and online/LAN flows.

Tracker-Visual.log and Saved/Screenshots/Tracker-Above-Dark.png, Tracker-Below-Bright.png, Tracker-Visible.png and Tracker-Hidden.png exercise elevation, contrast, marker transition and hiding. These isolated visual fixtures inject transforms/reveal state; they are not a multiplayer proof. Bush-Max-Range.png and Bush-Low-Range.png render the same production HISM bush rows at 35, 80, 160 and 350 metres: Max retains distant rows, while Low culls them. The post-review position-marker label adds RUNNER to distinguish it from objectives.

First review: visual restraint 5/10, tracking/elevation 6/10, foliage control 5/10, integration/performance 6/10. The most valuable change was removing the world-space vehicle decoration and placing a small indicator in established HUD space. The three main refinements were a stronger arrow hierarchy, explicit target labeling for the on-screen marker, and live preset updates with predictable world distances. Final target: visual restraint 8/10, tracking/elevation 8/10, foliage control 8/10, integration/performance 9/10. Ring vertices and arrow triangle storage are reused; target text and LOS refresh at 10 Hz. There is no new per-frame target actor scan or dynamic material instance.

Local replicated tracking and final Shipping deployment results are recorded below when complete. No new physical two-device internet test or low-end hardware performance benchmark is claimed. Remaining subjective tuning is compass placement, text size and proximity pulse strength.

