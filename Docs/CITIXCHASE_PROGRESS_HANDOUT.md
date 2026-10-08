# CitixChase progress and playtest handout

Updated: 4 October 2026. Unreal Engine 5.8. Project: `E:/UnrealProjects/CitixChase/Citix.uproject`.

## Try the game

1. Close older game windows, then launch `D:/_YienStudio/CitixChase/Windows/Citix.exe` (latest complete game build).
2. First window: select **Host Match**. Second window: enter `127.0.0.1` and select **Join Match**. For two PCs, copy the entire `D:/_YienStudio/CitixChase/Windows` folder to the other PC and enter the host PC's LAN IPv4 address instead.
3. Wait until both driver slots show connected, then both players select **Ready**. Roles appear prominently during the countdown. A match has two rounds with roles swapped; results offer **Ready for Rematch**.
4. Keyboard fallbacks: H hosts, J joins localhost, F readies. Click the game viewport before driving if the address box still has keyboard focus.

Controls: W/S accelerate, brake and reverse; A/D steer; Space handbrake; Shift boost; R stuck-car reset; C camera; F exit/enter; hold F for capture. Relays and gates activate by crossing. On foot: WASD, mouse look, Shift sprint. The chaser fires with the left mouse button. The runner uses left mouse to deploy smoke in the car or on foot.

## Current rules

- Runner: drive or walk through all five relays; entering the 13.5 m area activates immediately, without stopping or pressing F. Completed relays stay complete after a wreck. Five activate the two escapes. Blue road arrows guide toward the next relay/escape. Relays have a blue rotating holographic projection instead of the tall solid pillar.
- Breakaway gates: target about 100, with at least 200 m separation along connected roads; current default city fits 86. Nearby arms of an intersection share at most one gate, and models are kept away from junction centres. Counts depend on the generated city and safe road footprint. Runner crossing grants five seconds of free boost and yellow energy streaks. One shared 15-second boost cooldown across all gates; recharge HUD and grey gates remain. Leave and cross again after recharge; staying inside does not retrigger.
- Chaser: gates show red crosses and STOP markers. Crossing independently ramps speed down by up to 30% over 0.75 s, holds, then smoothly releases before the 1.2-second effect ends. Red drag particles and a brief HUD status show the penalty. Runner cooldown does not disable this hazard.
- Chaser: win by capture, four pistol hits, the runner's second destroyed car, a lethal run-over above 40 km/h, or the five-minute timeout. Capture requires an uninterrupted two-second hold within 2.5 m and clear sight.
- Four separate qualifying rams wreck the runner's 100-health car. Sustained contact does not repeatedly damage it. Chaser cars cannot be destroyed.
- First wreck: character health becomes 50; the runner tumbles for three seconds and stands over 0.65 seconds. Protection ends with recovery. A 40-second replacement countdown begins at the wreck; its remaining time leaves the runner vulnerable.
- After the countdown, F claims a clear, unoccupied, unowned road car within 3 m, moving strictly below 10 km/h. One reserve per round. Traffic conversion retires the source only after successful creation and possession. Replacement car health is full; character health and relay progress remain unchanged.
- A functioning owned car can be exited and re-entered without another cooldown or repair. The second wreck immediately awards the chaser a win before ejection or recovery.
- Pistol: 15 bullets maximum; one regenerates every eight seconds below capacity, including while driving during pursuit. Further shots do not restart the active refill timer. Four confirmed hits on an unprotected on-foot runner win; entering a car or wrecking does not clear the counter. Hits cause brief slowdown without reducing character health.
- Runner smoke: one charge initially, maximum two; regain one every 60 seconds while below capacity. Extra uses do not restart an active refill. LMB consumes one charge and emits from the runner's current car/on-foot position over five seconds, leaving a roughly 35 m wide, uniform-grey low-poly trail. Earlier puffs remain where they were released. It lingers and fades by 15 seconds total; another use is rejected during the five-second release. Charges and refill survive exit/re-entry and wrecks, and reset each round. New emissions follow possession changes; older puffs stay behind. Smoke is visual concealment; it does not form a physical wall. A compact runner HUD shows charges, release time and refill.
- Runner handling: stronger normal steering (.98 authority), more high-speed lock (.82 retained), quicker input response and increased lateral grip. Chaser handling and both absolute speed caps remain.
- Runner horizontal cap: 228 km/h. Chaser: 292.5 km/h (30% increase from 225). Chaser engine force is also 30% higher. Boost and breakaway cannot exceed these limits. Body appearances use canonical chase physics. Runner engine force starts at 3 times baseline below 96 km/h, tapers smoothly through 168, then uses 0.78 times baseline.
- Traffic: population target 225, local cap 39, existing interpolation retained. Traffic impacts now cause ordinary physical disruption only; the scripted chaser stun and its HUD are removed.
- Reveals last ten seconds every 25 seconds. Chaser vehicles stay red after replication and swaps.
- Shared lighting cycle: night 101.25 s, dawn 67.5 s, day 33.75 s, dusk 67.5 s; total 270 s. The starting hour remains 19:00.

## Implemented presentation and networking

A brief role-specific rules card appears in the centre during the shared ten-second countdown and disappears at pursuit. The chaser's target status shows runner car integrity while driving, and pistol hits while the runner is on foot.

Native Signal Grid lobby: prominent Host/Join, address entry, connection state, two driver slots, Ready and keyboard focus. Failed connections show a short useful message. Gameplay uses role badges, five relay icons, four integrity segments, pistol-hit marks, ammo/refill, recovery progress and contextual prompts. The centre stays clear.

Pistol arm orientation follows the shoulder-to-hand direction; the grip is anchored at the hand. Local recoil, muzzle particles and synthesized firing/dry-click sounds provide immediate feedback; confirmed hits produce red particles and a hit marker without duplicating local firing effects.

Joining clients render timestamped traffic roughly 100 ms behind the shared server clock. Extrapolation stops at 100 ms, and histories clear for visibility changes, recycling and large teleports. Simulation and collision decisions remain server-owned.

A shared river/bridge, ground and footprint query guards spawns, entry, ordinary and forced exits, recovery and reset. Vehicles retain their last safe dry pose and discard unsafe velocity near water. Reset preserves damage and boost. Unsafe layouts remain in the lobby with an error instead of using an origin/water fallback.

## Verification evidence

| Check | Evidence |
|---|---|
| Build and Shipping archive | `Saved/Logs/clear-final-package.log`; packaged completion receipt recorded below |
| Rules, exact clock boundaries, speed curve/caps, recovery boundaries, refill capacity/timing | `Saved/Logs/clear-final-automation.log` |
| Traffic midpoint interpolation, bounded extrapolation, recycle and teleport history clearing | Same native automation test |
| Geometry across seeds 7, 1337 and 2026 | Same test; dry roads and bridge corridors retained, river rejected |
| Physical cruising across six car bodies | `ChaseMP_clear_speed_verified_Host.log`: all twelve role/body probes reached 190/225 without exceeding caps, including boost and alternating breakaway variants |
| Native Host/Join/Ready activation | `ChaseMP_clear_buttons_key_*.log`, `clear720_final`, `clear1080_verified`; actual Slate focus/Enter activation, not direct travel shortcuts |
| Traffic takeover at 10 / 9.99 km/h | `ChaseMP_clear_traffic_takeover_Host.log`: rejects exactly 10, retains reserve, adopts below 10 with health 50/full car, retires source |
| Recovery, early rejection, reserve, second wreck, role swap and rematch | `ChaseMP_clear720_final_Host.log`, `clear_exit2_Host.log` |
| Four hits at health 50, persistence through replacement entry/exit, victory and rematch | `ChaseMP_clear1080_verified_Host.log`; final Shipping completion receipt includes one F near a relay |
| Server excessive/empty firing rejection and unchanged refill deadline | `ChaseMP_clear_ammo_Host.log` |
| Ordinary shoreline exit, water restoration, reset preserving health/boost | `ChaseMP_clear_safety_green_Host.log`, `clear_seed7_Host.log` |
| Capture interruption and capture victory | `ChaseMP_clear_capture_Host.log` |
| Timeout and second escape | `ChaseMP_clear_timeout_exit2_Host.log`, `clear_exit2_Host.log` |
| Disconnect | `ChaseMP_clear_disconnect_Host.log`: remaining driver returns to phase 0 with one player and zero round wins; no result awarded |
| Failed join | `ChaseMP_clear_failure_verified_A.log`; `720p/ChaseConnectionFailure-A.png` shows the concise error and intact lobby |
| Original preservation | `clear-original-verification.json`: 142 tracked original source/config files checked, zero changed |

Use `Docs/TWO_PC_PLAYTEST_PROTOCOL.md` for the normal-control acceptance run and ten-match recording table.

Final Shipping receipt: `Docs/evidence/clear-pursuit/packaged-test-receipt.json` records completed rounds/rematch and `pistol_reentry: true`. That final scenario claims a replacement with one F beside a relay, then exits and finishes the four-hit victory. Shipping normal logs are disabled; the receipt is generated only by the opt-in test after completing this flow.

Some multiplayer scenarios use two local processes with 80 ms simulated packet lag, 20 ms variance and 3% packet loss. These are automated scenarios, not ten human matches. Shipping disables normal logs; its opt-in test completion receipt provides packaged loop evidence.

Rendered evidence is under `Docs/evidence/clear-pursuit`: 720p, actual 1080p, and packaged 1440p. `-ForceRes` was required to prevent Windows from clamping larger test windows. Early 1423x889 captures are review drafts, not proof of 1080p/1440p.

## Review and practical limits

Three refinement passes fixed the blank late-created lobby, unsafe initial poses, route backtracking, inconsistent body physics, speed/boost overlap, crowded pointers, unsafe ordinary exits, inherited sandbox health capacities and verbose connection errors. A fresh independent review identified the ordinary-exit issue; it now passes a shoreline regression probe.

Current craft assessment: UI clarity 8/10, guidance 8/10, recovery completeness 8/10, visual coherence 7/10. Driving responsiveness and fairness remain provisional at 6/10 until normal-control two-PC playtests. The existing owner prediction reconciles velocity; it does not replay buffered inputs. Pistol/camera test runs require rendered viewports; the harness now enables rendering for that scenario because NullRHI can leave remote camera positions stale.

Remaining human validation: ten paired matches with comparable players; actual LAN connectivity on separate PCs; steering/drifting under real network conditions; audible recoil/fire feedback; traffic smoothness around collisions; long shoreline/bridge drives and blocked recovery; and balance statistics. Automated collision/interaction fixtures deliberately position actors to exercise the rules and do not establish normal-input driving feel or proven fairness.

Do not launch the old package while evaluating the update. Use the complete newly archived Windows folder.


## Screenshot feedback update — 4 October

The speed card has separate rows for the speed, KM/H and boost meter, with wider padding. Road chevrons meet at their tip without overlapping arms and use a bright blue emissive shader. The native yellow breakaway gate is road-aligned, visible from both approaches, collision-free and replicated to both players. Activation and expiry use the shared authoritative clock; manual boost input cannot cancel station power, and the 190 km/h runner ceiling still applies.

Evidence and review notes: `Docs/evidence/station-polish` and `Docs/STATION_POLISH_REVIEW.md`. The 225 speed readout in these screenshots is an explicit display-layout fixture, not a claim that the runner physically travels at 225. Actual role speed limits remain 190/225.


Latest archive: `Saved/Logs/station-final-archive.log` — BUILD SUCCESSFUL. Native final rules: `Saved/Logs/station-delivered-rules.log` — RoundRules Success. Final archive SHA256: `AD500D861F8058189C75E67A0C1AB4B8FAA87C6B1605A207F9888CD03CD085DF` (child Shipping executable, 4 October 11:44 MDT). `Docs/evidence/station-polish/packaged-round-receipt.json` confirms complete rounds/rematch and `station_passed: true`; the last archive changes only HUD pointer padding after that gameplay test. Final native 1080 screenshot/activation checks and packaged Host/Join/Ready visual smoke cover the final UI.


## Earlier delivery: client re-entry, relay dwell and headlights — superseded relay/gate rules

- Joining-client re-entry now removes the outgoing character input context even after its controller is cleared. Client possession reinstalls driving mappings; replicated occupancy releases parking brakes and stale boost/handbrake inputs.
- Relays activate after two uninterrupted seconds in their existing 13.5 m area, in a car or on foot. Leaving cancels progress; F release does not. The HUD shows a blue synchronization ring and countdown instead of a held-F instruction.
- Player cars have two actual light beams, aligned to each appearance's headlamp models. Intensity follows the synchronized day/night system, fades out in daylight, and switches off on a wreck. Lights do not cast extra shadow maps.
- Chase drift steering authority increases from 1.0 to 1.25; retained anti-slide assist increases from 30% to 75%, with lighter yaw damping. Ordinary handling, runner agility and the 190/225 km/h caps remain.
- Initial re-entry regression failed with throttle 0 and speed 0.2 km/h. The corrected joining client reached 120.0 km/h through simulated W input, and 103.3 km/h in a rendered test with 80 ms lag, 20 ms variance and 3% packet loss. These are test observations, not top-speed measurements.
- Final native relay regression: car activation 2.01 s, on-foot activation 2.02 s; movement interrupted activation; ordinary re-entry retained the reserve. Native round-rule automation passes.

See `Docs/REENTRY_DWELL_REVIEW.md` and `Docs/evidence/reentry-dwell` for review and delivery evidence. Actual two-PC playtesting and subjective drift tuning remain human verification tasks.


Final delivered archive: `Saved/Logs/reentry-final-package.log` — BUILD SUCCESSFUL. Packaged Host/Join/Ready plus client exit/re-entry/W regression passed at 121.18 km/h and throttle 1.00. Receipt: `Docs/evidence/reentry-dwell/packaged-reentry-receipt.json` (4 October, 12:24 MDT). Child Shipping SHA256: `8FB2A656A267549FB1735906CC36A9E64914070997093CECDF8AC7DCC531CF97`. Final native round receipt confirms escape, swapped-role wreck recovery, second-wreck defeat and rematch. Final packaged screenshots were inspected; headlights and the revised relay instruction are visible. Original source/config verification: 142 checked, zero changes.


## Latest gate expansion delivery

The current rules above supersede the earlier two-second relay dwell and one-use station descriptions in historical evidence. See `Docs/GATE_EXPANSION_REVIEW.md` and `Docs/evidence/gate-expansion` for fresh verification. Balance, real two-PC feel and long sessions with denser traffic still need human playtests.

Final gate build (4 October): `Saved/Logs/gate-final-clearance-package.log` — BUILD SUCCESSFUL. Native rules pass; feature probes pass with simulated latency and city seed 1234. Seed 5678 completes escape, role swap, first-wreck recovery, second-wreck defeat and rematch. The final packaged Host/Join/Ready feature probe passes with 20 gates, shared cooldown/reuse, independent chaser drag, real traffic-impact stun, five instant relays and target traffic 225. Dry-road height is validated under every gate; only edge posts collide. Screenshots and receipts are in `Docs/evidence/gate-expansion`. Child Shipping SHA256: `4B8C834FCA4932D53B412FF21E8AF526F3409F5824A8EEC175CDD45C4B765DAB`. Original baseline verification: 142 files, zero changes.

Close older game windows before starting `PackageShipping/Windows/Citix.exe` so the new executable is loaded. Runner gate recharge appears immediately above the speed card. Cross once for five seconds of yellow energy; all gates remain grey until the shared 15-second cooldown expires. Chaser gates remain red hazards during runner recharge.

## Latest approximately 150 m gate-spacing delivery

Gate distribution is no longer capped at 20. The default test city has 340 gates; placement follows dry road lengths, targeting intervals <=150 m. A coverage probe checked 1,329 dry-road checkpoints and found a maximum connected-road distance of 69.9 m to a gate. Native gameplay regression and rendered packaged Host/Join/Ready checks with seed 1234 pass, including dry ground beneath gates, cooldown/reuse, chaser drag, traffic stun and five relays. Receipts and inspected 720p screenshots: `Docs/evidence/gate-spacing`; review: `Docs/GATE_SPACING_REVIEW.md`.

Shipping archive: `Saved/Logs/gate-spacing-final-package.log` — BUILD SUCCESSFUL. The initial archive destination was locked by running game windows, so the build was archived to `PackageGateSpacing`, then copied into the normal `PackageShipping` folder once those windows closed. Both contain the same child executable SHA256 `503BF935F28D1448CE5AB6550AE7DF71E6B84C9FCCD0B8ED45164D96B093D112`. Close old windows and use the normal launcher above. Original source/config baseline: 142 checked, zero changes. Long-session performance and pursuit balance still need human playtests.

## Latest delivery: sparse gates and runner smoke

The current launcher is `PackageSmoke/Windows/Citix.exe`. The older `PackageShipping` archive was left intact because its game processes were running; it does not contain these latest changes. Close older windows and launch the new archive on both sides.

Full Shipping build/cook succeeded in `Saved/Logs/smoke-package.log`. The new child executable SHA256 is `5D0785A91CDA67D1271CFFC296250B1554E5735260943C7C9AE9FB4D0021A72D`. Native round-rule and gate regression checks passed. The cooked joining client exercised real LMB in car and on foot, gradual emission, real 60-second refill and cloud expiry against a headless editor authority. `Docs/evidence/smoke-gates/cooked-client-receipt.json` records passed=true, 86 gates and 200 m spacing; cooked screenshots show both smoke views. This is mixed editor-host/Shipping-client evidence, not a completed two-Shipping-instance smoke run. A later attempt on the older normal archive produced no smoke receipt and is not counted as verification.

Original preservation was checked again in `Saved/Logs/smoke-original-verification.json`: 142 tracked original source/config files, zero changes. Separate-PC latency, prolonged smoke performance, handling feel and match balance still need human playtesting.

## Chaser speed follow-up

The PackageSmoke launcher now includes the 30% chaser increase: 292.5 km/h absolute ceiling and 1.3 times previous engine force. Runner speed and handling remain unchanged. This supersedes historical 225 km/h statements. The fresh native rules suite and Shipping rebuild/stage/archive passed (`chaser30-rules.log`, `chaser30-package.log`). The existing cooked assets were reused because this is native-code-only. Original verification again found zero changes across 142 tracked files.

Live multiplayer physics evidence: `ChaseMP_chaser30-speed-final_Host.log` records the chaser at 8125 cm/s (292.5 km/h) during pursuit after successful remote login. The short probe ended before its full six-body completion report, so this confirms attainable speed on the initial body rather than a full appearance/boost matrix. Earlier probe attempts failed to connect and are not counted as driving evidence. Shipping child SHA256: `87AB44389BACD34EDEF9C13E996916B1E5A85A73B470F1D025107A9FB5A4343D`.

## Gate drag duration follow-up

Chaser gate drag now lasts 1.2 seconds, superseding earlier three-second descriptions. The existing 30% slowdown and smooth ramp/release remain, as do the runner five-second boost and shared 15-second cooldown. `gatedrag12-rules.log` verifies the new exact duration, gradual start, full slowdown and expiry.
Shipping build/stage/archive passed in gatedrag12-package.log; the updated files are delivered through the existing PackageSmoke launcher.

## Runner speed follow-up

Runner car cap increased 20% from 190 to 228 km/h, with the engine curve scaled by 1.2 in force and speed thresholds to support the increase. Chaser cap remains 292.5 km/h. Runner walking is now 510 cm/s versus chaser 340; runner sprinting is 1155 cm/s versus chaser 770, exactly 50% faster in either mode. Existing stagger and gate penalties still apply afterward. Role movement is reapplied on server and client; no cumulative multiplication. Earlier runner speed descriptions are superseded.

Delivery: use `PackageRunner20/Windows/Citix.exe` on both sides. PackageSmoke was running and remains the previous build. `runner20-rules-final.log` passed the full native rules suite, including both 1.5 on-foot ratios, car cap and force scaling; `runner20-package.log` passed Shipping rebuild/stage/archive with existing cooked assets. Fresh original verification: 142 tracked files, zero changes. Child executable SHA256: `3387AE8DAD726F46E769E7F39780FC5FFDA5CB31E85741404F8460D4B2BFEE41`. No new remote driving or human balance test was performed for these tuning values.

## Trailing smoke and D-drive delivery

The latest complete game is archived at `D:/_YienStudio/CitixChase/Windows`, with launcher `Citix.exe`. Previous E: packages remain older snapshots. Runner car remains 228 km/h, chaser 292.5; gate drag remains 1.2 s; runner foot movement remains 1.5 times equivalent chaser movement.

Smoke uses forty instanced 20-triangle grey billows with a dithered masked fade. The server records birth positions for five seconds and replicates them; each client renders the same trail locally. A stationary release is about 26 m across, 30% larger than the former 20 m cloud. Moving extends the trail along the path, rather than translating already released puffs. Smoke is visual concealment; it does not block bullets or become a physical wall. Charge/refill rules remain one initial, two maximum, one per 60 s.

Fresh evidence: `trail-red-rules.log` fails the previous billboard geometry, and `trail-final-rules.log` passes the dedicated mesh, 20-triangle count, 1.3 diameter scale, ten-second briefing, final-wreck invariant and full existing rules suite. `ChaseMP_trail-visual_*.log` covers actual LMB, both clients receiving all 40 birth positions, a 10 m emitter movement leaving the first puff behind, real 60-second recharge, expiry and on-foot use. `trail-polish` repeats moving-emitter rendering with the final masked material. `ChaseMP_trail-final-wreck_Host.log` verifies four separated accepted rams on a reserve-used fixture produce an immediate chaser result without ejection/recovery. `trail-d-drive-package.log` completes full Shipping build/cook/stage/archive, including the new smoke mesh/material.

Visual evidence and receipts are in `Docs/evidence/trailing-smoke`. Native 720p views of both role briefings, smoke, car-target status and on-foot pistol status were inspected. The source/config preservation check again found zero changes in 142 tracked original files. Separate-PC latency, long-session effects performance, human handling feel and role balance remain human-playtest limits.

Packaged delivery proof: two actual Shipping instances from D: activated Host/Join/Ready, ran the moving smoke scenario, used LMB in a car and on foot, waited the real 60-second refill and confirmed cloud expiry. Fresh receipts `packaged-smoke-receipt.json` and `packaged-trail-client-receipt.json` are in `Docs/evidence/trailing-smoke`. Packaged screenshots of both briefings, full smoke and chaser pistol status were inspected at 720p. Shipping child SHA256: `579B1435FBA9F002486D4665BAF0385B15632FE8BB7FDE350156C644C00B8B5B`. `Play CitixChase.cmd` and `START_HERE.md` at the D: archive root provide a short launch path. This local dual-instance test does not establish real LAN latency or role balance.

Packaged final-wreck proof: `packaged-final-wreck-receipt.json` records passed=true, immediate_second_wreck=true and no_recovery=true after four separated rams on the reserve-used car fixture. Both packaged test processes are owned and closed by the harness.

## Denser uniform-grey smoke and countdown lock

Latest D: rebuild increases puff count from 40 to 100 (+150%, 2.5 times), expands horizontal scatter radius 1.5 times, widens billows in XY and flattens Z, with less upward rise. A stationary cloud is approximately 35 m across. The material uses a constant neutral grey emissive colour and smooth opacity, with no texture, procedural colour noise, per-face shade or temporal dither. Five-second trailing emission, charges and lifetime remain.

Countdown lock: authority holds both occupied cars at their spawn poses with physics disabled for Countdown only. Local drive handlers reject non-pursuit inputs, clear caches, and driving physics restores with zero velocity at Go. It preserves health and boost. `ChaseMP_dense-grey-countdown_*.log` injects throttle/boost requests throughout countdown, confirms both cars remain fixed and physics/input resume at pursuit. The same run confirms gradual 28/100 early emission, all 100 replicated births and first puff stays behind. `smoke-dense-rules.log` passes after `smoke-density-red.log` fails the former forty-puff count. `smoke-dense-d-package.log` completes full Shipping build/cook/archive to the same D: folder.

Visual review at native 720p: the ground-hugging cloud is markedly wider and uniformly grey; formerly noisy face shading is absent. HUD remains legible. No extra visual features added. Running two rendered games while cooking yielded variable 13–28 ms diagnostic frames; this is not a controlled performance test. More overlap and real match balance still require human playtesting. The short smoke check does not rerun the unchanged 60-second resource cycle; its earlier complete verification remains recorded above.

Delivered Shipping verification: two actual D: game instances activated Host/Join/Ready. Fresh packaged-countdown-receipt.json confirms both_cars_locked=true and released_on_go=true; packaged-trail-receipt.json confirms the replicated 100-puff trail. The cooked screenshot was inspected and is uniform grey with wider horizontal spread. Child SHA256: `BB423F0EABB3CADD1E9466263A378ABE821182609BE82F62A3E8834FE7592229`. Original preservation again: 142 checked files, zero changed.

## Road start correction
Both cars are placed on validated roads before the ten-second countdown. Initial client interpolation no longer substitutes a zero/origin position. Invalid placement returns to the lobby. Native host/client road placement, countdown lock and Go release checks passed. Delivery and fresh packaged evidence are recorded in Docs/SAFE_START_REVIEW.md.


## Per-PC performance and Esc menu
Automatic first-launch CPU/GPU benchmark chooses a conservative graphics tier. This PC measured CPU 241.9/GPU 405.2 and qualifies for Max. Esc opens native Low/Medium/High/Max buttons, saved manual overrides, Automatic and Resume. Low/Medium scale only the 3D scene to 70/85 percent; HUD remains native resolution. The menu blocks local driving/looking and releases held inputs without pausing the network match. Gameplay simulation is preserved. Low-PC FPS remains to be measured on the target hardware. See Docs/GRAPHICS_REVIEW.md for evidence and review.

