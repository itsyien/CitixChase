# Ice Wave: local scan, world frost

Implemented 2026-10-07. The car previously could overtake a scan anchored at its activation position. The scan now uses the emitter car's current bumper position and planar heading after physics each frame. The replicated EmitterCar reference is separate from FrozenTarget, so both viewers animate the scan from the same car and server start time.

The wave actor stays at its original world transform. Only ScanArc and the leading crystals move with the car. Frost is deposited as up to 24 decal bands during the existing 0.85-second sweep. Every stamp captures its world transform, origin, heading, inner radius and outer radius once; subsequent frames change only its fade. Band thickness accounts for movement and turning between samples to reduce gaps at speed or low frame rates. Pixels outside the band skip the detailed frost calculation. No footprint history is reconstructed for frames a client did not observe.

The frozen runner shell still belongs on the affected runner's mesh. The ground frost does not follow either car. The authoritative activation cone, 30% initial slowdown, three-second freeze, charges and ninety-second recharge are unchanged.

## Verification

- IceSpace-Red.log: the new behavior test failed before implementation, including scan translation/heading and immutable frost radius.
- IceSpace-FinalSuite.log: all 17 automation tests passed. The new LocalScanWorldFrost test exercises a 9,000 cm position change with a 75-degree heading change, fixed earlier decal transforms/radii, replicated emitter declaration, emitter destruction, a bounded decal budget and full thaw. This tests effect transforms, not a natural driving trajectory.
- ChaseMP_ice-local-world-final_Host.log: PASS for activation, 30% speed cut, engine/boost lock, recovery, recharge and reserve cap.
- ChaseMP_ice-local-world-final_A.log: PASS for replicated freeze and release in the actual rendered client.
- Saved/Screenshots/IceWave-Host.png and IceWave-A.png: fresh rendered effect inspected; the warmed host image shows the detailed frost band. The client capture still includes a preparing-shaders indicator, so client gameplay assertions provide the reliable replication evidence. The rendered fixture's chaser was stationary; high-speed movement/turning is covered by the automation transform test.
- IceSpace-Package-v1.0.log: Shipping BuildCookRun succeeded, exit 0. Updated D:/_YienStudio/CitixChase deployment verified 34 files and every SHA-256 against the new release manifest. The deployed Shipping game was responsive after its startup smoke test. Version remains v1.0; use the updated build on both PCs.

## Self-review

Initial: fast-driving fidelity 4/10, coordinate separation 4/10, visual coherence 8/10. The fixed activation origin and continually advancing ground mask prevented the requested motion behavior. Final: fast-driving fidelity 8/10, coordinate separation 9/10, visual coherence 8/10, simplicity/performance 8/10. Refinements keep the existing decal count cap, avoid expensive frost work outside each band, and preserve the existing freeze/gameplay rules. Low frame rates and sharp turns have movement-aware bands; exact ground stamp history is cosmetic and can differ with client timing. A manual high-speed driving pass remains useful for subjective appearance.

Source backup: Saved/Backups/ice-local-world-20261007-174711. Previous deployed v1.0 backup: Saved/Backups/deployment-before-ice-space-20261007-175423.
