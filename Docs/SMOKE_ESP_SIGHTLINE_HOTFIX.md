# v1.2 smoke ESP sightline hotfix

The previous visibility rule tested only the Runner pawn's center against smoke puffs. A Runner could drive beyond the emitted smoke while the trail still blocked the Chaser's view, and retain their ESP. The old stationary-cloud tests did not exercise this arrangement.

The server now checks both Runner containment and the finite sightline between the two pawns against the existing deterministic smoke ellipsoids. The query reuses visual growth, fade, world birth positions and elevation. It creates no temporary arrays. Smoke beside, behind or on another elevation does not hide the Runner. Expired smoke does not conceal. At 150 m or farther, tracking remains visible regardless of smoke.

The existing replicated reveal flag continues to control the ESP square, offscreen marker, ground direction ring and Runner label together. No duplicate UI concealment system was introduced. Game version remains v1.2 and multiplayer protocol remains 4; the host must run this hotfix to use the corrected server visibility rule.

## Verification

- Before the fix, the real GameMode regression failed: a smoke trail between cars 100 m apart did not hide tracking (`Saved/Logs/SmokeESP-RedTest.log`).
- After the fix, all 30 Citix automation tests passed, exit 0 (`Saved/Logs/SmokeESP-Suite.log`). Coverage includes containment, finite sightlines, 150 m override, trail expiry, lateral separation and elevation.
- Two local editor game processes exercised a trail between the cars, hidden at 140 m, visible at 150/160 m, hidden on return, and restored after cloud removal. Both processes observed all four replicated transitions. Receipts and hidden/visible screenshots: `Docs/evidence/smoke-esp-hotfix`.
- Shipping BuildCookRun completed with exit 0 (`Saved/Logs/SmokeESP-Package.log`).
- Installed Shipping host/client checks using the real lobby Host/Join flow passed the same 140/150/160 m trail and restoration sequence. Both processes observed four replicated transitions. Evidence: `Docs/evidence/smoke-esp-hotfix/shipping`.
- Replaced `D:/_YienStudio/CitixChase`; all 31 release files including the manifest were hash-verified (`Saved/Logs/SmokeESP-Deployment.txt`). Debug-only symbols/fonts are omitted from this runtime package. Previous installed release: `Saved/Backups/deployment-before-latest-20261008-201704`. Owned test instances were closed; the user's separate running game was retained.

## Product review

The most important refinement was making the whole tracking presentation obey smoke obstruction through the shared authoritative decision. Concealment fidelity improved from 5/10 to 8/10; presentation coherence 8/10; verification confidence 8/10. Remaining limits: local host/client testing does not establish behavior on a new physical two-PC internet playthrough, and the finite sightline models pawn viewpoints rather than every camera orbit.
