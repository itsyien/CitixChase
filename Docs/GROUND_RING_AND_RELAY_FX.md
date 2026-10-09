# Ground navigation ring and relay pickup sprinkles

## Final behavior

The floating HUD compass has been replaced with a local-only ground effect centered under the occupied chaser vehicle. A thin, gently segmented cyan ring is 6.4 m across. Its dim body follows car heading and road slope; a brighter chevron and short edge arc rotate independently toward the runner's horizontal world direction. The existing shortest-angle exponential smoothing and authoritative reveal/phase/role gates are retained. No camera yaw enters the world bearing. Reveal loss hides the effect immediately; loss of HUD target updates, vehicle possession, or valid ground also hides it. The ring does not reveal an otherwise hidden runner.

Projection uses two instanced plane batches rather than new per-segment components or a new decal material. One Pawn-channel road trace per visible local ring positions it 2 cm above the surface. Roads deliberately ignore Visibility/vehicle traces, so the Pawn channel is essential. The ring aligns to the local surface normal; it approximates one planar road patch rather than wrapping every curb. No collision, shadows, or navigation contribution. Static geometry is initialized once; the ten highlight instances reuse their transforms with a single render-state update. The base ring has 48 small chords and twelve subtle gaps. This uses existing emissive materials and is visible at night without a dynamic light or elaborate shader.

A successful server-authorized relay collection sends one reliable multicast to existing clients. Each renders 24 small blue and pale-cyan sprinkles using the existing shrinking, spinning low-poly spark actor. They start around the runner, inherit part of its movement, rise briefly, and disappear within 1.1 seconds. The effect supports both vehicle and on-foot collection. Invalid/repeated relay entries are rejected before the cosmetic event. Dedicated servers render neither effect. Spark actors are created only on collection events, with bounded lifetime; no new per-frame arrays or materials are created.

The previously requested bush range fix remains: Low 8–12 m, Medium 90–160 m, High 250–400 m, Max 450–650 m, updated on preset changes.

## Review and verification

The highest-impact review finding was frontward arrow occlusion by the car. A short bright arc around the chevron now preserves a visible direction cue along the ring edge. The other improvements were reducing the fixture to a single 24-sprinkle burst and reviewing from a camera that showed the entire car/ring. Initial review: directional hierarchy 5/10 because the car could fully hide the front chevron. The highlight arc was widened and the ring radius refined from 3 m to 3.2 m after checking the whole scene. Scores after refinement: immersive composition 8/10, directional hierarchy 8/10, responsiveness 8/10, low-poly restraint/performance structure 9/10. Remaining limits are ordinary world occlusion and planar projection across abrupt curbs; no hardware-wide performance benchmark is claimed.

- Fresh automation: 25 passed, zero failed, exit 0 (`Saved/Logs/GroundRing-VerifiedSuite.log`). Covers production road projection, road height, reveal hiding, draw-batch count, spark count/lifetime, angle wrap/smoothing, existing graphics/physics/ice/host-travel regressions.
- Local two-process listen-server/client game: ground-ring receipts on both host and remote client after round roles swapped. The remote client receipt confirms `replicated_target:true`. Both received relay cosmetic multicasts and both recorded reveal-hidden transitions. Receipts are copied into `Docs/evidence/ground-ring`.
- Rendered visual fixtures reviewed for dark/bright background, target in front/behind/above/below, and hidden state. Fixtures inject positions/reveal state and are visual evidence, not another physical-device test.
- Final Shipping BuildCookRun succeeded with exit 0 in 43.64 seconds (`Saved/Logs/GroundRing-FinalPackage-v1.0.log`). Deployed to `D:/_YienStudio/CitixChase`; all 34 deployed files and the release manifest were hash-verified. Version remains v1.0. Previous deployment is preserved at `Saved/Backups/deployment-before-ground-ring-20261007-191737`.
- Installed Shipping build: real LAN host button and real EOS online host button both reached verified listen rooms. Online used CitixEOSNetDriver. A local packaged LAN match rendered the final 3.2 m-radius ground ring and delivered the relay burst to both host/client; reveal hiding was recorded. The editor host/client test additionally verified the remote client tracking a replicated target after role swap. No new physical two-device test was performed for this cosmetic update.
- Earlier two-process receipts contain the pre-polish 3 m radius. Final installed Shipping host receipt confirms 3.2 m. Tracking/reveal logic was unchanged by the sizing/arc polish.



