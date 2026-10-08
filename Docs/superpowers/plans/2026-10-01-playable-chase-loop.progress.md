# Execution ledger — plan: Docs/superpowers/plans/2026-10-01-playable-chase-loop.md

Pre-flight: no Git repository exists at `E:\UnrealProjects\CitixChase`; implementation proceeds in the user-authorized remake without commits or worktrees.

Pre-flight: Task 1 produces a HUD assignment and chase-only input handling. Tasks 2, 3, 4, 6, and 7 consume chase state/input paths; their existing names and intended interfaces match the plan.

Task 1: HUD assignment and chase-only input isolation implemented. Standalone Development and Live Coding builds succeeded; normal player-control acceptance remains pending.

Tasks 2-4: round/match result separation, per-player server interaction holds, and second-wreck character-health resolution implemented. The two-process smoke run completed round-one escape, swapped-role round two, first-wreck recovery, second-wreck defeat, and match results. Normal-control and latency acceptance remain pending.

Follow-up authority pass: PreLogin rejects a third driver; server drive/reset requests are rejected outside pursuit and destroyed cars cannot reset. Rule coverage includes exact ram speed/cooldown boundaries. Forced ejection now requires a grounded, capsule-clear nearby location rather than always spawning through geometry. Standalone build and smoke run `task2to5` completed through both round outcomes without a failure; its 55-second duration ended before the final rematch assertion.

Drive-freeze follow-up: the authoritative vehicle tick now clears all control inputs outside pursuit, including the listen host's direct input path. Editor module compilation succeeded.

Task 5: road routing now excludes non-drivable edges and supports same-node routes; a lightweight route regression case was added. ChaseGameState replicates the generated city seed, layout hash, and server-selected relay/exit/spawn/replacement locations. Remote clients now report their local generated seed/hash to the server; readiness is refused until it matches. Road-distance placement coverage and rendered route measurements remain pending.

Task 5 recovery placement follow-up: candidate parking is generated near every relay and exit before road snapping. After wreck, only the runner's two closest candidates are spawned and beacon-marked. Editor module compilation succeeded; route-distance/safe-walking validation remains pending.

Task 7: added the C++ `UCitixChaseLobbyWidget` with Host Local Match, Join Address, and Enter Lobby controls. Host uses native listen-server travel; join uses native client travel; the lobby reports the two-driver waiting state. Splitscreen is disabled and the player guide was updated. The editor module compiled successfully. Rendered host/join and failure-path tests remain pending.

Task 6: replicated time until the next reveal and updated the chase HUD to distinguish `YOU ARE REVEALED` / `RUNNER REVEALED` from the next reveal countdown. Existing chaser tracker remains the current visual indicator; outline material, hit/recovery feedback, and rendered visual review remain pending.

Task 8: current Win64 Shipping target built successfully. A fresh no-build UAT cook/stage/archive succeeded while the editor's Live Coding session remained open; output is `PackageShipping\\Windows\\Citix.exe`. Packaged host/join and latency acceptance remain pending.

Task 8 release refresh: after the final drive-freeze and replacement-layout changes, a fresh standalone Development build and fresh no-build Shipping cook/stage/archive both succeeded. Packaged host/join and latency acceptance remain pending.

Task 1 HUD cleanup: chase mode now suppresses the GTA sandbox's weapon, shop, wanted, race, job, discovery, activity, and sandbox panels at the shared HUD entry point. The standalone Development build and fresh two-process full-loop regression both succeeded; the latter passed rematch restart.

Chase-rule cleanup: the real chassis callback already routes car contact into the server ram validator. The separate legacy on-foot run-over sweep now returns immediately in ChaseGameMode so it cannot invoke sandbox damage or ordinary death. The standalone Development and current Shipping build/package both succeeded.

Task 5 route validation: snapped spawn locations now select a connected drivable road route in the 22,500-45,000 cm band, and recovery candidates must route within 10,000 cm of a relay or exit. Current generated-city evidence measured a 32,678 cm spawn route and retained four recovery candidates. Multiple physical approaches and safe walking remain normal-playtest acceptance items.

Task 8 package refresh: the route-validation source compiled in Shipping and the fresh no-build cook/stage/archive completed successfully.

Task 6 reveal clarity: the chaser now gets a local screen-space bracket around an in-view revealed runner, or direction/distance when they are off screen; it resolves the runner's current pawn so it remains valid on foot. Development build succeeded; rendered visual review remains pending.

Disconnect fix: live guest disconnect exposed `GetNumPlayers()` as stale inside `Logout`, preventing cancellation. Chase logout now resets the match unconditionally (the mode permits two drivers only). A fresh host/guest run logged the cancellation and no winner result.

Authority check: a live host, guest, and third-client attempt verified the PreLogin gate. The third client received `CitixChase is full (two drivers maximum)` and did not join.

Task 8 package refresh: reveal-locator and disconnect-fix source compiled in Shipping and fresh cook/stage/archive completed successfully.

Capture recovery fix: the capture scenario exposed the safe ejection search as too small for actual city collision. It now searches grounded, capsule-clear rings out to 2,400 cm without allowing blocked spawns. A fresh capture run passed first wreck/recovery, post-ejection protection, capture hold, chaser win, and match result.

Task 8 package refresh: the ejection recovery source compiled in Shipping and fresh cook/stage/archive completed successfully.

Latency regression: host and guest both ran with 150 ms packet lag and 30 ms variance. The complete two-round flow, replacement claim, second wreck, match result, and rematch passed without a city mismatch or test failure. This remains automated-hook evidence, not ordinary-control playtest evidence.

GTA-input cleanup: late replicated chase state could leave a client with SandboxContext/TimeContext already mapped. The controller now refreshes contexts every tick and removes both in chase mode, preventing maps, jobs, weapons, shops, races, photos, car swapping, and time scrubbing from becoming reachable through an early join race. Development/Shipping builds and refreshed package succeeded.

Task 4 follow-up: forced ejection now reports success/failure. A failed grounded/capsule-clear placement queues a protected retry every 0.5 seconds during pursuit, and a new round clears pending ejections. Editor module compilation succeeded; blocked-ejection runtime verification remains pending.

Regression run `task4to8`: fresh Development build succeeded. The two-process host/guest smoke test passed countdown, three relays/exits, round-one escape, role swap, first wreck/recovery, second-wreck defeat, match result, and rematch restart. The test harness still uses authoritative setup hooks and is not normal-control evidence.

Acceptance coverage follow-up: added harness switches for an alternate exit and forced pursuit timeout. `task18-timeout` passed the live two-process host/guest timeout path with `Chaser wins: Time expired.` `task18-second-exit` passed round one through exit two and then completed the existing swapped-role/recovery/match/rematch regression. Both remain authoritative-hook evidence, not normal-control evidence.

Task 8 package refresh: current Win64 Shipping build and fresh cook/stage/archive succeeded after the acceptance-harness additions (`task18-shipping-build.log`, `task18-current-package.log`).

Self-review pass 1: core pursuit and round fairness score 7/10 from current authority evidence; clarity and visual coherence score 6/10 because no rendered normal-control session has assessed them. The existing time-of-day, fog, rain, street-light, and night-sky systems are already retained by chase mode; adding a parallel atmosphere stack would be redundant. The highest-impact next pass is a normal-control two-PC visual/playability session, then tune the existing defaults from recorded matches.

Self-review improvement: added a local-only 0.6-second red impact flash and role-specific callout whenever the existing replicated chase status confirms an accepted ram. `task19-impact` completed the full two-process regression after this presentation-only change; Shipping build and package refresh passed. Visual review is still needed to assess the result in motion.

Self-review improvement: the chaser's reveal indicator is now a local screen-space frame projected from the current runner pawn bounds rather than a fixed bracket. It applies to either the car or on-foot runner and keeps the existing off-screen direction/distance signal. Development and Shipping builds plus package refresh passed (`task20-reveal-outline-*`).

Interaction acceptance coverage: `-CitixChaseTestRelayHold` exits the runner through the ordinary path, starts a relay hold, moves away to prove interruption, then completes three 3-second holds through `BeginInteraction` and the hold tick. It re-enters the original functioning car, escapes, and completes the ordinary role-swap/recovery/match/rematch regression. `task21-relay-hold-host.log` passed; Shipping build and package refresh also passed.
