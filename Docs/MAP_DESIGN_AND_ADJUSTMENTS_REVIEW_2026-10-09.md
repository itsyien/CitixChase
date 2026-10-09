# Hillside Switchback — design and requested adjustments

This brief records the original design-first request. The active goal subsequently authorizes completing all adjustments and building the low-poly map. Current execution and evidence live in ADJUSTMENTS_IMPLEMENTATION_PLAN_2026-10-08.md, ADJUSTMENTS_VERIFICATION_2026-10-08.md, and superpowers/plans/2026-10-09-hillside-switchback.md. This brief does not claim implementation or verification is complete.

## Map concept

![Hillside Switchback overall structure](map-design/Hillside-Switchback-Structure-2026-10-08.png)

A connected coastal hillside chase map: a fast coastal bypass, central town terrace loop, broad switchbacks climbing to a radio-tower summit, an alternate ridge descent, and a tunnel shortcut. A town clock tower and marina provide additional orientation landmarks. West and east escape gates have alternate approaches; separated start areas avoid immediate spawn contact.

Cyan markers illustrate candidate relay sites, not a finalized layout. Select active relays from a fixed authored pool; completing five unlocks escape. Exact pool and active counts remain design choices. Suggested fairness policy: retain the same subset across role-swapped rounds, then reroll for a new match.

Later, the host chooses Existing City or Hillside Switchback in the lobby. Guests see the selection. Both players must load the same map before starting. Preserve the existing city.

Before building: validate car turning clearance, grades, tunnel clearance, connected junction elevations, objective travel times, and visible boundaries. The concept image is illustrative rather than measured construction geometry.

## Fixes and adjustments

1. **Guest abruptly stops:** investigate invisible vehicle collision or authoritative movement correction. Identify the actual blocking actor/component before fixing it; preserve collisions with visible vehicles.
2. **Five relays to escape:** change the requirement from six to five, including rules and all player-facing progress text. Four must remain insufficient.
3. **Invisible city-edge walls:** remove stray collision where visible models are absent, or align necessary boundaries with visible geometry. Avoid opening unintended voids.
4. **Crash after runner escape:** reproduce and inspect escape, results, teardown, and round transition. Verify both roles, both rounds, and rematches.
5. **Replacement vehicle integrity:** requested second vehicle after the first explodes has two integrity points. Treat this as a provisional interpretation; the wording could instead describe a bug. Ordinary re-entry must not reset an intact vehicle's integrity.
6. **Guest loses control in round two/restart:** verify possession, controller ownership, input state, and resets through role swaps and rematches.
7. **Settings volume:** add a local persistent master-volume control, applying immediately, with zero muting game audio.
8. **Air-cutting effect:** display only above 80% of that car's configured top speed. Replace three moving lines with broad tapered translucent cyan/white airflow ribbons around the car, following the supplied reference. Maintain road visibility.
9. **Runner's own smoke:** deploying runner sees smoke at one-third opacity. Other players retain normal opacity; authoritative gameplay coverage remains intact. Check overlapping smoke and vehicle changes.
10. **Bullet trails:** short-lived visible trails from muzzle to resolved shot endpoint, stopping at cover. Preserve damage, ammunition, and firing rules.
11. **Relay randomization:** server selects active sites from existing authored locations, replicating identical sites and progress to both players. Inactive sites cannot be completed.
12. **Stable first-person driving camera:** C-key first-person camera stays at the window relative to the car, without velocity-based backward movement. Preserve third-person camera behavior.

## Airflow reference

![User-provided visual direction](map-design/Neon-Aircut-Racer-Reference.png)

The attachment supplies visual direction only; it does not contain task instructions. Car styling and scene lighting in the reference are not requested changes.

## Review standard

After any later major implementation, apply the user's self-review loop: compare against intended qualities, score honestly, identify the three highest-impact weaknesses, improve the most important first, and repeat up to three major cycles. Validate the complete driving and host/guest experience, not isolated screenshots. Record evidence and remaining limitations before claiming completion.

## Whole-map review, first improvement pass (work in progress)

Reviewed the actual curved-route aerial render and live road handling. Scores before this pass: low-poly coherence 6/10, route choice 6/10, driving readability 7/10, multiplayer trust 6/10, lobby clarity 4/10. Backend identity travel works, but this is not a polished release score.

Highest-impact weaknesses: landmarks lack explicit road access, broad empty terrain feels accidental, lobby map controls lack a visual preview and full interaction proof. The first improvement adds connected marina/summit service roads and restrained groves using existing shared foliage/trunk meshes. Reserved areas keep rail barriers away from landmark entrances. Revision advances to 3 to distinguish the changed layout. The next review must inspect actual renders and car access, not assume these additions succeed. Lobby presentation and packaged multiplayer remain open.
