# CitixChase — two-PC play guide

Open `Citix.uproject` from this folder with Unreal Engine 5.8. The project name shown in Unreal is **CitixChase**. The first screen provides **Host Local Match**, **Join Address**, and **Enter Lobby**. Hosting restarts the generated-city map as a listen server. Joining accepts a LAN address such as `192.168.1.20`; the host supports exactly two drivers.

During each runner reveal, only the chaser sees a cyan segmented ring above their own car. Its arrow rotates toward the runner, including when the runner is on foot; it disappears when the reveal timer ends.

On the host PC, use **Host Local Match**. For two PCs on the same network, enter the host PC's LAN address on the second PC. Once both drivers enter the lobby, each presses **F** to ready the round. Splitscreen is disabled because CitixChase is a two-PC game.

## Round controls

- **WASD / left stick:** drive or walk
- **Space / B:** handbrake
- **Shift / LT:** boost
- **LMB while driving as chaser:** Ice Wave — a 40 m, 120° forward scan. Starts with one charge, stores two, and recharges one every 90 seconds during pursuit. A hit immediately removes 30% of the runner's current speed and locks acceleration and boost for three seconds. Steering and braking still work. Another hit refreshes the freeze without multiplying the speed loss.
- **LMB as runner:** smoke. **LMB as an on-foot chaser:** pistol.
- **F:** in the lobby/after results, press to ready (both drivers must ready); in a car it exits; on foot it enters the nearest free car, and **hold it** on foot to activate a runner relay or to capture the runner
- **C:** swap driving camera
- **R:** right an overturned car

The runner completes three orange relay beams, then drives to one of the blue exit beams. The chaser rams the runner’s car four times to cause a wreck, then captures the runner on foot. A first wreck costs 50 character health and grants one replacement car (spawned at a marked location — walk up and press F to claim it); a second wreck ends the round. The runner is marked for three seconds at the start and for two seconds every 25 seconds.

The chaser’s car is never destroyed. Capture needs the chaser on foot within 2.5 m of the runner with a clear line of sight, held for two seconds (a wreck grants three seconds of capture protection).

## Reading the HUD

The top-left panel shows the round timer, completed relays, your car’s hull %, your
character health, and how many of the four rams have landed, plus the current objective
and any hold progress. The bottom of the panel reminds you of the current action.

Each match runs two rounds and swaps roles after the first result countdown. On the final results screen, both players press **F** to start a rematch.

