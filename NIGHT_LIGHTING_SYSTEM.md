# Citix — Night Lighting System

How the city is lit after dark, why it works, and every knob available to tune it.

## Why the city looked weak at night

Three concrete causes, all architectural rather than "not enough lights":

1. **Every window in the city shared one material instance.** Window bands used two
   emissive surfaces (`WindowWarm` / `WindowCool`) with a single cached
   `UMaterialInstanceDynamic` each, so all 6,000+ bands were identical. No variation
   was possible, so the skyline read as a flat field of the same panel.
2. **The lit area was a thin lip.** Bands were only 30% of a floor in height and 0.8%
   proud of the facade, so from any distance they were sub-pixel.
3. **Auto-exposure fought the look.** A dark night scene made auto-exposure lift
   everything to mid-grey, so the "night" looked like washed daylight and the lit
   windows had no contrast against the facades.

## The system

### 1. Per-window variation via a variant palette (no custom material needed)

Windows are grouped into **nine emissive variants plus an "off" state**:

| Surface | Meaning |
|---|---|
| `WindowWarmBright` / `WindowWarmMid` / `WindowWarmDim` | warm office/home lighting, three brightness tiers |
| `WindowCoolBright` / `WindowCoolMid` / `WindowCoolDim` | cool blue office lighting, three tiers |
| `WindowWhite` | neutral white (retail / modern) |
| `WindowGold` | saturated decorative gold (landmark accent) |
| `WindowOff` | dark glossy glass — the "lights off" case |

`FCitixSurfaceLibrary::GetWindowVariant` / `IsWindowSurface` expose the family.

**Each band picks its own variant** inside `AddFloorBands`, so lit and unlit windows
mix naturally down a facade and between neighbours, instead of one look per building.
Because a material instance is shared per variant, this costs nothing per instance —
each variant is its own instanced mesh component per chunk.

### 2. District lighting profiles

`FCitixDistrictLightingProfile` (Project Settings → Game → Citix City → Night
Lighting) drives the character of each district:

| Field | Effect |
|---|---|
| `LitChance` | fraction of window bands that are lit at all |
| `BrightnessBias` | 0 = mostly dim windows, 1 = mostly bright |
| `CoolChance` | how often the blue office palette is used instead of warm |
| `AccentChance` | chance of decorative facade/crown lighting on towers |
| `BandHeightScale` | how tall the glazing bands are (density of lit area) |

Defaults, Shanghai-flavoured:

| District | Lit | Bright | Cool | Accent | Band |
|---|---|---|---|---|---|
| Financial | 0.94 | 0.82 | 0.45 | 0.85 | 1.15 |
| Downtown | 0.85 | 0.64 | 0.38 | 0.55 | 1.05 |
| OldTown | 0.74 | 0.34 | 0.06 | 0.20 | 0.95 |
| Residential | 0.63 | 0.36 | 0.18 | 0.12 | 0.90 |
| Industrial | 0.46 | 0.30 | 0.14 | 0.06 | 0.85 |
| OuterCity | 0.56 | 0.36 | 0.22 | 0.12 | 0.90 |

### 3. Skyline richness

`AddTowerAccents` adds, to any tower taller than 60 m that passes `AccentChance`:
- **corner light strips** running 92% of the building height (warm gold or cool blue),
- a **lit crown ring** just under the roofline,
- a **red aviation warning light** on the roof.

Accent strips are placed on the archetype's *shaft* width (e.g. 0.62× for podium
towers) so they sit on the tower rather than floating off the podium.

### 4. Street level

`AddStorefront` now lights the ground floor: a lit shopfront band (55% bright warm,
otherwise a district-appropriate window variant) plus 1–4 illuminated **sign boxes**
in warm or cool emissive. Street lamps, headlights and taillights all use emissive
surfaces and glow with the same night boost.

### 5. Real street lighting at a fixed cost
`ACitixStreetLightSystem` pools **34 shadowless point lights** and re-targets them at
the nearest of ~318 recorded lamp heads, refreshing every 0.25 s or after 25 m of
movement, with intensity fading in with darkness. No lamp owns a light, so the cost is
constant regardless of city size.

### 6. Buildings emit light (opaque emissive window material)

**The root cause:** the engine's `EmissiveMeshMaterial` is **additive (translucent)**, and
**Lumen ignores translucent surfaces for global illumination**. So the windows *looked*
lit but emitted nothing — the city had no light coming from its buildings.

**The fix:** two authored materials, built by an editor commandlet (see below):

| Material | Used by | Why |
|---|---|---|
| `M_CitixWindow` | window bands | Opaque, unlit, emissive = `Color × PerInstanceRandom × pane mask`. Being **opaque** puts it in the Lumen surface cache, so lit windows contribute real GI. `PerInstanceRandom` gives every band its own brightness; the **procedural pane mask** (`frac(UV.x × PaneColumns)` → `Step(0.16)`) cuts dark mullions so a band reads as a row of individual windows. All of it costs **no draw calls and no extra instances**. |
| `M_CitixEmissive` | lamps, vehicle lights, signs | Opaque unlit emissive = `Color`, so street lamps and headlights also bounce light. |

Both are `bUsedWithInstancedStaticMeshes = true` (required for HISM instances) and use a
`Color` vector parameter, which is what `SetEmissiveBoost` / district palettes drive.

**Authoring the materials** (one-time, and after any regeneration of them):

```
Engine\Binaries\Win64\UnrealEditor-Cmd.exe <project>.uproject ^
  -run=CitixMaterialSetup -unattended -nosplash -stdout
```

Produces `/Game/Citix/Materials/M_CitixWindow` and `M_CitixEmissive`. The runtime
`FCitixSurfaceLibrary` loads them and **falls back to the engine's additive material** if
they are missing (with a warning in the log), so the project always runs.

Verify which is in use in the log:
`[Citix] Using authored opaque emissive materials (windows contribute to Lumen GI).`

Also added this session: a pool of **16 shadowless building lights** re-targeted at **569
recorded building emitter points**, plus a warm night sky-light tint
(`NightSkyLightIntensity` 2.6), which is the cheap way to bathe the whole city in light.

### 7. Atmosphere, sky and wet roads

- **Night sky dome**: `/Engine/EngineSky/SM_SkySphere` scaled to enclose the slice,
  using `/Engine/MapTemplates/Sky/M_Procedural_Sky_Night` with `ZenithColor`,
  `HorizonColor`, `StarColor` and `RimColor` set to a deep blue night with a faint warm
  horizon. It is **hidden from scene capture** so the sky light does not treat it as
  the sky and flood the city with light.
- **Exposure**: an unbound post-process volume drives `AutoExposureBias` from
  `-2.2` at night to `0` by day. This is the single most important control — without
  it auto-exposure washes night into daylight.
- **Wet streets**: `SetNightWetness` lowers the roughness of asphalt/markings/sidewalk
  (0.82 → 0.22 for asphalt) and darkens them slightly, so roads catch the street lights
  and window glow. Driven by `bWetStreetsAtNight`.
- **Haze**: height fog density drops to 0.006 at night (so distant towers keep their
  glow) with a cool inscattering colour for a light-pollution horizon.
- **Bloom**: raised to 1.5 at night (`NightBloomIntensity`) so bright windows bleed —
  this is what makes the skyline read from far away. `DayBloomIntensity` is 0.7.

### 7. Time of day

`ACitixTimeOfDay` drives everything. `DayLengthMinutes` (24) sets the cycle length and
`NightDurationScale` (2.2) slows the clock between 18:30 and 06:00 so night lasts
roughly twice as long as the equivalent daytime hours (about 25 min of night vs 12 min
of day).

## Tuning

| Want | Change |
|---|---|
| Brighter/dimmer windows overall | `ACitixTimeOfDay::NightEmissiveBoost` (6.0) — the primary night-brightness lever |
| Scene darkness (contrast) | `NightExposureBias` (−1.85): more negative = darker facades, brighter-looking lights |
| Windows brighter in daylight | `DayEmissiveBoost` (0.55) |
| Darker night overall | `ACitixTimeOfDay::NightExposureBias` (more negative = darker) |
| More/fewer lit windows | district `LitChance` |
| Brighter district | district `BrightnessBias` |
| Thicker lit bands | district `BandHeightScale` |
| More landmark lighting | district `AccentChance` |
| More lit streets | `ACitixStreetLightSystem::MaxLights` / `LightIntensity` |
| Less/more wet-road reflection | `bWetStreetsAtNight`, or `SetNightWetness` values |
| Dazzle / skyline glow | `NightBloomIntensity` (1.5) |
| Density of lit floors | `CitixBuildingGenerator.cpp` `BandStep` (`Floors > 80 ? 2 : 1`) |
| Longer night | `NightDurationScale` |
| Sky colour | `CreateNightSkyDome` (`ZenithColor` / `HorizonColor` / `StarColor` / `RimColor`) |

Runtime: `Citix.Time <0-24>`, `Citix.TimePause <0|1>`, or scrub with `[` / `]`.

## Performance

- Windows are **instanced meshes**, one component per variant per chunk. Variation
  costs components, not lights: ~30,000 instances at night.
- The window material achieves per-window brightness variation with a single
  `PerInstanceRandom` node — **no extra draw calls, no extra material instances**.
- Dynamic lights are **capped at 26 street + 16 building = 42**, all shadowless.
- Nothing per-window ticks or is spawned individually.
- Measured: **97–102 fps @1600×900** with 150 traffic + 60 pedestrians + 30 parked
  cars + 42 lights, at night with the star dome, wet roads and Lumen GI from windows.
  The opaque emissive material cost roughly 3–4% versus the previous additive one.

## Known limitations

1. **Windows are bands subdivided by a shader mask, not true geometry panes.** The
   `PaneColumns` mask gives the *look* of individual windows at zero cost, but panes do
   not align to real floor-level mullions and pane width is uniform per facade face.
   True geometry panes would multiply instances ~10×.
2. **The authored materials must be regenerated if deleted** — run the
   `CitixMaterialSetup` commandlet. Until then the runtime silently falls back to the
   engine additive material (no GI from windows).
3. **Auto-exposure is very sensitive** and remains the main brightness lever — see the
   tuning section before adjusting night brightness.
4. **The night sky dome is opaque and hides the SkyAtmosphere** once dark, so scrubbing
   time quickly can pop.
5. **The dome's brightness is exposure-normalised**, so changing its colours changes the
   sky's *hue* far more than its brightness.
6. **Street/building lights are moving pools** — travelling very fast can outrun the
   0.25 s refresh.
7. **District profiles only affect generation**, so `Citix.Regenerate` is needed to see
   profile changes on an existing city.
8. **No volumetric light shafts** (too costly at 42 lights).

## Session update — per-pane windows, skyline variety, street level, atmosphere

The complaints were: buildings read as *a few glowing strips*, windows were not varied,
towers were repetitive, street level was dark and empty, and the atmosphere was too clean.

**1. Per-pane window variation (the big one, zero instance cost).** Previously only a whole
*band* was randomised: one `PerInstanceRandom` per floor, and every pane in it shared that
brightness — which is exactly why a facade read as uniform glowing strips. `M_CitixWindow` now
also hashes the pane index:

```
PaneIndex = floor(UV.x * PaneColumns)
hash      = frac(sin(PaneIndex * 12.9898 + PerInstanceRandom * 78.233) * 43758.5453)
level     = saturate((hash - PaneOffFraction) / (1 - PaneOffFraction))
Emissive  = Color * PerInstanceRandom * mullionMask * level
```

`PaneOffFraction` (0.26) is the fraction of panes that go dark; the rest get graded brightness
from the same hash. So individual windows now differ down and across every facade, for no
draw calls and no extra material instances — all arithmetic in one unlit opaque material.
`PaneColumns` is 12 and the mullion gap 0.12, so panes read smaller and more window-like.

**2. Richer bands, dark floors, per-building character.** Band height went from 30% to **46%
of a floor** (more lit area, more prosperous). `AddFloorBands` now skips dark floors with a
chance scaled by district (sparser districts get more), plus a short *vertical* dark stripe, so
a tower is never uniformly lit. `MakeFacade` perturbs the *district* profile per building
(`LitChance` × 0.72–1.12, `BrightnessBias` ±0.14, `CoolChance` ±0.12), so two towers on one
street are no longer equally full of light — which is the cheapest possible fix for
"too many towers look repetitive".

**3. Skyline variety.** `AddTowerAccents` used to give every tall tower the same four corner
strips. It now picks a style per tower: four corner strips, two opposite strips, a lit crown
hovering at the top, or a whole glowing top section — and **every** tall roof gets its red
aviation light, which is what makes a skyline look inhabited.

**4. Street level.** Every building now gets a lit ground floor from one place in the
dispatcher: shopfronts in walkable districts, a narrower cooler **lobby** band elsewhere, plus
a bright entrance/canopy glow and more, larger sign boxes. Ground-floor lit chance went 0.55 →
0.72.

**5. Atmosphere.** Night fog up slightly (0.006 → 0.009) for humidity, bloom up (1.5 → 1.85)
so the skyline bleeds, stars dimmed (0.55 → 0.22) so the sky reads as a hazy city night rather
than a star field, and a warm horizon/rime (light-pollution glow) that is now ~6× brighter than
the zenith.

### Tuning

| Want | Change |
|---|---|
| More/fewer dark panes | `M_CitixWindow` parameter `PaneOffFraction` (0.26) |
| Pane size | `M_CitixWindow` parameter `PaneColumns` (12) and the mullion gap constant (0.12) |
| Taller/shorter lit bands | `CitixBuildingGenerator.cpp` band height (0.46 of a floor) |
| More/fewer dark floors | `AddFloorBands` `DarkFloorChance` lerp (0.34 → 0.08 by district) |
| Per-building variety | `MakeFacade` perturbation ranges |
| Accent style mix | `AddTowerAccents` `Style = Rng.RandRange(0, 3)` |
| Street-level brightness | `AddStorefront` lit chance and `bLobby` band |

**Performance:** no new lights, no new draw calls from the window change (it is arithmetic in
the existing material), and dark floors *remove* instances. Measured 106 fps at 1600×900 in a
night aerial with 54 traffic + 50 pedestrians, and 132 fps at 1280×720 in the ragdoll test.

### Still open

1. **The ragdoll→get-up handoff.** Three concrete defects were fixed (the pose blend started
   immediately and could displace the body — it now holds the captured pose for the first
   third; `GetBodyLocation` followed the torso mid-animation and could throw the test camera
   off — it now reports the actor once the get-up starts; and the recovered body is lifted onto
   the surface it fell on, so it can no longer end up inside the pavement — the probe now
   reports 12.0/15.9/15.9 cm instead of 0.0). **One residual frame remains**: in the ragdoll
   test camera, at ~5.4 s the body is not rendered even though it is visible at 5.2 s and 5.6 s.
   The camera is stable at that spot across all three, the parts are forced visible, and the
   body's own recorded resting height is on a surface, so the remaining suspect is the *captured
   pose* itself (`RecoveryStartTransforms` recorded from parts that were still moving when
   `BeginGetUp` fired, placing the frozen body outside the camera's narrow framing). Next step
   is to log each part's world position across the handoff frames rather than infer it.
2. Shopfronts and signs are lit boxes, not real frontage detail.
3. No animated signage or blinking strobes.

## Session update 2 — Shanghai direction, and why the night was invisible

**The night *was* enabled but you could not see it.** `ACitixTimeOfDay::StartHours` was **8.0**, so
pressing Play gave you morning; the geometry changes are generation-time (they do rebuild on
Play) and the window material needed the commandlet to be re-run. The default start is now
**19:00 (dusk)**, so the lit city is the first thing you see.

To drive it yourself: `Citix.Time 21`, `Citix.TimePause 1` to hold it, or `[` / `]` to scrub.
After editing a *district* lighting profile, `Citix.Regenerate` is still required.

**Toward the reference (Shanghai dusk):**

| Change | Where |
|---|---|
| Dusk start instead of 08:00 | `CitixTimeOfDay.h` `StartHours` / `Hours` = 19 |
| Warm gold dominance: `CoolChance` lowered across all districts (Financial 0.45 → 0.20, Downtown 0.38 → 0.18, Market 0.05 → 0.03) | `CitixCitySettings.cpp` `AddLighting` table |
| Tower accents: gold 62%, blue 23%, warm white 15% instead of one colour per tower | `AddTowerAccents` |
| Lit greenery: foliage 0.055/0.17/0.06 → 0.095/0.30/0.10, grass likewise | `CitixSurfaceLibrary::GetColor` |
| Darker roofs so lit windows dominate the frame | `Roof` 0.20 → 0.115, `RoofDark` 0.12 → 0.070 |
| Light-pollution horizon tinted violet/pink instead of brown | `CreateNightSkyDome` `HorizonColor` / `RimColor` |
| Windows no longer clip to white: emissive boost 6.0 → 3.8, bloom 1.85 → 1.25 | `CitixTimeOfDay.h` |

**Traffic now knocks pedestrians down.** The pedestrian hitbox also overlaps the *vehicle*
channel, and the overlap handler accepts `ACitixTrafficVehicle` — so contacts cost nothing to
poll (150 cars would have been 9,000 distance tests per frame). Traffic actors are teleported,
so their reported velocity is zero and a nominal city speed is used as the impulse.

**Decision on the open question (per-district pane character):** not done. `PaneColumns` /
`PaneOffFraction` are material parameters on a *shared* material, so per-district panes would
mean a material instance per district per colour — more state for a subtle difference. District
character already comes through `LitChance` / `BrightnessBias` / `CoolChance` / `BandHeightScale`,
which is the cheaper lever. Revisit only if the panes still look uniform in the review.

**Honest gap versus the reference.** The reference's facades are almost entirely *lit glazing* in
warm gold; ours still show a lot of grey unlit stone between the bands, so the city reads
grey-and-speckled rather than golden. The unlit facade base colours are also too light relative to
the lit windows. Next concrete step: raise the band height again (or make the facade material
itself read as glazing between bands) and darken the facade stone, so lit windows dominate the
frame the way they do in the photograph. The sky is also still deep blue rather than pink: the
dome is opaque and exposure-normalised, so its colours change hue far more than brightness.

## Session update 3 — restrained 4K night sky

The enlarged galaxy starfield has been replaced with Poly Haven's **Qwantani Night (Pure Sky)**,
a CC0 4096×2048 equirectangular EXR. `T_CitixNight.exr` is imported directly as linear HDR
(`TC_HDR`, sRGB disabled, wrap U/clamp V); no bloom preprocessing or lossy PNG conversion is
applied. Its source, authors, URL, licence and checksum live beside the asset in
`Content/Citix/Textures/T_CitixNight.LICENSE.txt`.

`M_CitixSky` is an unlit, two-sided dome material with `SkyTexture`, `SkyOpacity`,
`SkyExposure`, `SkyTint`, `HorizonColor`, and `SkyRotation`. The dome fades in only after dusk,
does not appear in scene captures, and falls back to the engine procedural sky when unavailable.
It uses normal depth testing so it stays behind the city rather than covering it.

**Facade glazing (the "grey city" fix).** Window bands went from 46% to **74% of a floor
height**, so the space between bands now reads as the sill/mullion line of a curtain wall rather
than as bare stone, and the facade stones were darkened roughly 40% (`FacadeConcrete`
0.52 → 0.30, `FacadeWhite` 0.76 → 0.46, `FacadeMetal` 0.38 → 0.24) so lit glazing dominates the
frame instead of pale concrete. Roofs were darkened earlier in the same way.

## Next recommended improvements

1. **Author a window material** with per-instance custom data (or a window texture) so
   individual panes can be lit — the single biggest remaining visual step.
2. **Wet-road reflections**: Lumen reflections on the wet asphalt are working, but a
   proper wet-road material with puddle masks would sell it far better.
3. **Interior glow / shopfront interiors** and more street furniture lighting.
4. **Light shafts** from a handful of hero lights (volumetric fog on a small radius).
5. **Aviation strobes** that blink, and animated signage.
