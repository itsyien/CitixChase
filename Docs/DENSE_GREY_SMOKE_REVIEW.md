# Dense grey smoke and countdown review

Intent: +150% denser smoke, constant grey/no noise, more horizontal spread, no driving for ten-second ready countdown, rebuilt D: delivery.

Reviewed native full-cloud images for runner and chaser. The uniform-grey silhouette deliberately replaces faceted shade variation; wider spread and reduced rise cover the road rather than filling the skyline. The main HUD remains legible. The requested plain colour means less internal shape detail, which is intentional and should not be reintroduced as noise.

Scores: request fidelity 8/10; horizontal concealment 8/10; material simplicity 8/10; countdown correctness 9/10 with server test; performance confidence 7/10 without controlled match benchmark. Highest-impact problem was physics movement despite rejected inputs. Fixed by an authoritative pose/physics hold, client input gating and clean release at Go. Checks inject full throttle/boost in countdown on both cars and verify both remain held, then drive inputs are accepted afterward. No additional major refinement needed.

Use `Docs/evidence/dense-grey-smoke` for images and receipts. Unit rules and full Shipping cook/build passed. Resource timers and round rules were preserved. Real LAN latency and balance remain human-test limits.
