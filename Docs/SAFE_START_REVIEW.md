# Road start correction — 2026-10-04

Both round cars are created and explicitly placed at validated dry-road transforms before the game enters the ten-second countdown. Failed placement keeps the match in the lobby. The start transform replicates explicitly, including road-facing rotation; the client seeds interpolation from the actual spawned actor rather than an uninitialized replicated movement value. Placement also initializes the safe pose and countdown hold pose, with zero velocity.

Native checks passed for dry role-specific starts, joining-client position, full-throttle/boost rejection throughout countdown, and physics/input release at Go. See `Docs/evidence/safe-start`. The joining-client countdown screenshot visibly shows the runner on the road.

Focused self-review: correct zone placement 9/10; transition clarity 8/10; movement lock 9/10. The highest-impact weakness was a valid spawn being overwritten by the client interpolation seed; fixed directly, with explicit start-pose replication as protection against packet order. No UI redesign or additional features were needed. Real remote-LAN latency remains outside this local test.

Shipping BuildCookRun succeeded (exit 0). Fresh two-instance packaged test from D:/_YienStudio/CitixChase passed host and joining-client safe-start checks and countdown lock/Go release. Shipping executable SHA256: 86D58606DEFE624CA83EA6F69482A9B5CB590EA0D9166053BF65D5411A484D8C. Original preservation check: 142 files, zero changed.

