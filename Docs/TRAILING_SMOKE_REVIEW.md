# Trailing smoke delivery review

Important qualities: smoke follows movement but leaves cover behind; grey low-poly shape fits the city; target status is contextual; final wreck is unambiguous; countdown teaches essentials briefly.

Pass 1: native gameplay verifies 40 sampled positions, approximately 10 m moved source and stationary first puff, LMB on foot/car, real recharge and expiry. Both role cards fit without overlap. Chaser integrity switches to pistol marks on runner exit. Weakness: overlapping translucent polygon faces look like crumpled glass. Highest-impact change: replace transparent sorting with engine temporal dithering in a masked material, so exterior facets read as grey billows and hidden interior surfaces do not compete. No extra meshes, texture downloads or particle framework.

Pass 2: inspect final native full/early views. Billows are distinct from round tyre smoke, conceal the road and retain polygonal outlines; HUD and centre tutorial are clean. Dedicated mesh is 20 triangles, 40 instances per cloud (800 triangles), no collision, shadow or navigation, and 20 Hz cosmetic updates. Replicated birth positions avoid per-frame particle networking. Test confirms final wreck finishes before ejection, even reserve-used health is unexpectedly positive.

| Quality | Before | After |
|---|---:|---:|
| Movement-following smoke | 3 | 8 |
| Low-poly visual coherence | 5 | 8 |
| Relevant target status | 5 | 8 |
| Final-wreck clarity | 7 | 9 with native evidence |
| Brief rule onboarding | 5 | 8 |

Scores describe inspected native behavior and screenshots, not proven balance or a controlled performance benchmark. Residual issues are subjective smoke density/shape and human testing at real LAN latency. Stop after two passes; further features would exceed this refinement request.

Final cooked review: two rendered Shipping instances from the delivered D: archive passed Host/Join/Ready and full smoke/recharge/on-foot scenario; the remote client received the same 40 trail births. Inspected the cooked full cloud and chaser on-foot status screenshots. Geometry remains cohesive with city, target state switches correctly and HUD stays legible. No additional major refinement warranted. Full build/cook/stage/archive succeeded; original preservation 142/142 tracked files unchanged. Final-wreck packaged check is recorded in the handout when its receipt arrives.
