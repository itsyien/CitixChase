# CitixChase two-PC acceptance and balance protocol

Use two separate PCs, or a host and remote client on the same LAN. Start the host from the lobby, join using the host PC's LAN IPv4 address (shown by Windows ipconfig), and run the checks below with normal controls only.

## Acceptance run

Run this once before recording balance matches.

- Both players enter the lobby, ready, and see opposite role instructions after the countdown.
- The runner completes three relays by holding **F** for three seconds, releases **F** or leaves range to confirm the meter interrupts, then re-enters the original car.
- The chaser lands four separated rams. The runner is safely ejected, loses 50 character health, remains protected through three seconds of ragdoll plus the 0.65-second stand-up, and sees the 40-second replacement countdown.
- The runner first confirms F entry is rejected before 40 seconds without using the reserve. After the timer, claim one clear unoccupied parked/traffic car within 3 m and below 10 km/h; confirm car health is full and character health stays 50. A second set of four valid rams ends the round. Confirm that holding **F** for capture breaks when range or sight is lost.
- Confirm four on-foot pistol hits win, including hits before and after vehicle entry. Car hits and protected recovery do not count. Exhaust the 15-round reserve, hear the dry click, and verify one bullet refills every eight seconds without later shots restarting the timer.
- Drive different car appearances with boost and breakaway; runner must stay below 190 km/h and chaser below 225. Chaser remains red after swaps.
- Approach a yellow station from each direction, then drive through with an empty boost reserve. Confirm five seconds of free boost/flames and a 30% chaser slowdown on both PCs. Effects must expire, the used station must dim and reject reuse, and stations must reset after the role swap.
- Activate a relay on foot from 9-13 m away; stepping beyond 13.5 m must interrupt the hold. Check speed digits at 144/190/225 for overlap and blue chevrons for a clear glowing arrowhead.
- Compare traffic motion and the shared 270-second lighting on both PCs.
- Approach shoreline and bridge edges, exit and reset; water must reject vehicles while bridges remain usable.
- In a separate round, complete both possible escape exits and allow one timeout.
- Press **R** on a functioning car while stuck. It must reposition without restoring its hull; a destroyed car must remain unusable.
- Disconnect either player during pursuit. The remaining player must return to the waiting lobby without a win award.
- Complete the second round, verify roles swap, then both press **F** at results to start a rematch.

## Ten paired matches

Play ten matches with the same pair of comparable players. Alternate which player hosts each paired match. A match contains two rounds, so every player runs and chases once.

| Pair | Host | Round 1 winner/reason | Time to first encounter | Runner recovered after wreck? | Round 2 winner/reason | Match winner | Notes |
|---|---|---|---:|---|---|---|---|
| 1 |  |  |  |  |  |  |  |
| 2 |  |  |  |  |  |  |  |
| 3 |  |  |  |  |  |  |  |
| 4 |  |  |  |  |  |  |  |
| 5 |  |  |  |  |  |  |  |
| 6 |  |  |  |  |  |  |  |
| 7 |  |  |  |  |  |  |  |
| 8 |  |  |  |  |  |  |  |
| 9 |  |  |  |  |  |  |  |
| 10 |  |  |  |  |  |  |  |

After ten pairs, calculate runner/chaser round-win rates, median first-encounter time, and recovery success rate. Adjust only the existing prototype values—ram damage, reveal interval, relay interaction radius, and round timer—then repeat the same protocol.


## Historical re-entry and automatic relay checks (4 October; relay dwell superseded below)

- Joining client: stop, F exit, F re-enter the same car, then W/A/D/Space/Shift. Repeat after role swap. Verify controls respond without repairing the car or consuming a reserve.
- Enter a relay area in a car. Remain for two seconds without F; confirm one completion. Leave before two seconds and return; confirm the timer restarts. Repeat on foot. Verify F still re-enters an owned car while relay progress is active.
- Follow a full lighting cycle: both cars illuminate the road at night, fade out by day and keep their beams aligned after adopted-car appearance changes. Check near a wall and across wet road markings for glare.
- Compare drifting to the preceding build at 60, 100 and 160 km/h. Use Space with steering; confirm easier nose rotation and less outward/forward carry without uncontrolled spins. Human feel and latency on separate PCs remain unverified by local native automation.


## Latest gate and relay rules — supersedes earlier relay dwell checks

- Runner crosses a gate: verify yellow energy/exhaust for five seconds, one shared 15-second HUD countdown, and all gates grey. Cross a different gate during recharge: no new boost or timer extension. After recharge, leave and re-cross; boost is available again. Staying inside does not retrigger.
- Chaser sees red cross/STOP gates. Runner using a gate alone must not slow the chaser. Chaser crossing any gate ramps to a 30% reduction over 0.75 s, then recovers by three seconds. Another crossing should extend active drag without a speed-cap reset.
- Check solid posts on both sides and overhead clearance on narrow and wide streets. Avoid bridges and water; no gate should turn a dry road into an impassable route.
- Both players inspect the blue holographic relays. Runner passes each of the five: immediate activation, one unique completion per relay, exits unlock only on the fifth. Repeat a relay, exit/re-enter a car, wreck/recover: completed progress persists.
- Chaser hits a traffic bot: two-second driving stun, red particles and status. Repeated scraping does not extend it. W/Shift/F cannot bypass it. Runner traffic impacts remain ordinary physical disruption. Check actual performance and traffic congestion on both PCs.

## Latest road-distributed gate placement

Gate count now follows the generated road network instead of a fixed 20-gate cap. Follow several long routes and intersecting streets: ordinary dry road segments have gates at intervals up to approximately 150 m, with clear junctions and no water placement. Confirm yellow/grey/red states and the shared cooldown still agree across clients. Drive at both role speed caps and check gate visibility at 400 m, approaches from both directions and extended-session performance. Tiny connectors and unsafe shoreline/bridge footprints may use a gate on an adjacent connected road.

## Current sparse gates and runner smoke (supersedes earlier density/stun rules)

- Follow several routes: gates should not form four-arm clusters, and have at least approximately 200 m connected-road spacing. Current default layout has 86, with a cap of 100. Test both directions and role swaps; retain shared 15-second recharge and five-second runner boost.
- Chaser rams a traffic bot: verify ordinary physical collision, without scripted braking, stun particles or a stun HUD.
- Runner steering: compare low, 100, 150 and 190 km/h under matching boost conditions. Verify easier turns, preserved speed cap and stable recovery from steering/drifting. Chaser tuning should match the previous build.
- Runner starts with one smoke, regenerates at 60 seconds and cannot store more than two. Use LMB in a car and on foot, during exit/re-entry and after a wreck. Verify charge/cooldown continuity, server rejection during release or when empty, and reset at role swap/rematch.
- Observe a deployment outside and inside: it must grow gradually for five seconds at the activation spot, obscure a roughly 20 m area with grey billows, differ from tyre smoke, and clear by 15 seconds total. Check both clients under latency, close buildings, shoreline and night headlights; capture/fire still require their existing rules.
