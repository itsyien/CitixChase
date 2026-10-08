# Road-based gate density

Request: provide a gate approximately every 150 metres rather than a fixed map-wide count.

Implementation: equally subdivide each eligible road segment into intervals no longer than 15,000 Unreal centimetres and place a gate at each interval midpoint. Segment ends are within 75 m of their first/last gate, so transitions between roads normally retain the 150 m maximum interval. Use existing dry-footprint and road-surface validation. Exclude bridges and tiny connectors (<35 m), preserve widths, role colours, global 15-second cooldown and five-second runner boost. No fixed maximum gate count or random sparse selection remains.

Performance refinement: gate status ticks at 10 Hz; geometry renders out to 400 m, with decorative light meshes not casting shadows. Physical posts remain active independently of visual culling. Relays keep their existing update rate.

Verification: road checkpoints every <=37.5 m independently measure distance along connected roads to an actual spawned gate, including adjacent roads for unsafe shoreline stubs. Eligible dry checkpoints must be within approximately 150 m; ordinary fully dry segments have <=150 m between gates and <=75 m to their nearest gate. Validate dry-ground height beneath every gate, then exercise cooldown/reuse, independent chaser drag, physical traffic stun and five instant relays through the existing paired multiplayer probe.

Self-review priorities: frequent usable escape opportunities; avoid junction clutter and water; keep role cues and cooldown legible; bound the render/update cost of denser placement. A first regression run fails the old sparse 20-gate count in `ChaseMP_gate-density-red_Host.log`.

## Review and fresh evidence

- Default city: 340 gates; 1,329 usable dry-road checkpoints checked; greatest connected-road distance to a gate is 69.9 m. All gate centres retain correct dry-road ground height.
- Editor build and native rule checks pass. `ChaseMP_gate-density-final_Host.log` and `evidence/gate-spacing/native-default-receipt.json` also confirm global cooldown and reuse, independent chaser slowdown, real traffic collision stun and five instant relays.
- Review pass: coverage 3/10 to 9/10 after replacing the sparse cap; role feedback remains 8/10; performance confidence 7/10 pending extended human playtests. The largest concern was adding distant draw/update cost; decorative lights have no shadows, geometry culls at 400 m and gate status updates at 10 Hz. Gate geometry, collision opening and existing HUD are retained.
- The initial per-segment-only coverage check incorrectly treated a safe shoreline stub as inaccessible because its gate was on an adjacent connected road. The final check uses road-network distance, so nearby gates across water or disconnected streets cannot hide a coverage gap.

- `gate-spacing-final-package.log`: archive succeeds. Rendered packaged Host/Join/Ready plus gate probe with seed 1234 succeeds: 340 gates, road coverage, cooldown/reuse, chaser drag, physical traffic stun and five instant relays. Fresh receipt: `evidence/gate-spacing/packaged-seed1234-receipt.json`. The initial headless packaged run produced no receipt and is not counted as a pass; the rendered rerun completes the check.
- Inspected packaged screenshots: `runner-packaged.png`, `chaser-packaged.png`, `relay-packaged.png` at 1280×720. Existing role colours, shared recharge display, restrained effects, label separation and hologram are retained. Long-session frame pacing and separate-PC feel remain unverified.
- Archive was made in `PackageGateSpacing` while running user windows locked the normal launcher. Once those windows closed, its contents were copied into `PackageShipping`; child SHA256 agrees in both: `503BF935F28D1448CE5AB6550AE7DF71E6B84C9FCCD0B8ED45164D96B093D112`.
- Original baseline: 142 source/config files checked, zero changes (`gate-spacing-original-verification.json`).
