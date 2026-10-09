# CitixChase adjustments implementation plan

User authorized implementation of the full design/backlog on 2026-10-08, followed by planning and creation of the low-poly Hillside Switchback map. Execute inline with independent debugging delegates; parent coordinates shared files and builds.

Spec: HILLSIDE_MAP_AND_REQUESTED_ADJUSTMENTS_2026-10-08.md.

Architecture: retain server-authoritative gameplay and existing procedural low-poly city. Reuse existing settings, effect, traffic, and round systems. Preserve all preexisting dirty work; baseline Source/Config copied to Saved/AdjustmentBaseline-20261008. Do not replace the installed package without a concrete verified build.

## Tasks

- [ ] Match lifecycle: inspect crash stacks and possession transitions, reproduce and pin meaningful regressions, repair escape and second-round state. Files: ChaseGameMode, ChaseRules, ChasePlayerState; coordinate PlayerController changes.
- [ ] Rules: five relays required, server-selected existing-site subset held across role-swapped rounds, replacement vehicle two integrity points. Files: ChaseRules, ChaseGameMode, RelayLayout, associated tests.
- [ ] Collision: inspect authoritative traffic visibility and client initialization, city boundaries and blockers; reproduce and repair demonstrated causes. Files: TrafficVehicle/System, VehiclePawn, City generation/surface code as evidence directs.
- [ ] Camera: first-person window anchor without velocity-induced displacement; TPS unchanged. Files: VehiclePawn and camera regression test.
- [ ] Audio: persisted local master volume with live application and readable settings slider. Files: SettingsWidget and Core settings, tests.
- [ ] Effects: air ribbons above 80% individual top speed, own smoke one-third alpha, short muzzle-to-hit tracers. Files: AirFlowComponent, SmokeCloud, tracer visual and shot feedback. Check rendered views.
- [ ] Build and verify fixes: editor automation plus actual host/guest transitions under latency, settings persistence and rendered effects. Investigate failures before map work.
- [ ] Map plan: resolve measured road topology, terrain/grade, objectives, spawn locations and host map-selection flow while retaining existing city.
- [ ] Map implementation: procedural low-poly Hillside Switchback, connected coastal/terrace/switchback/ridge/tunnel routes, landmarks, visible boundaries, map-specific relay sites and host-authoritative lobby selection.
- [ ] Full review and release verification: packaged two-map host/guest match loop, role swaps/rematch, screenshots, measured package size, up to three product refinement cycles.

## Review focus

Repeated escape transitions must not mutate collections during iteration. Guest possession must survive pawn destruction and role changes. Invisible/uninitialized traffic must not block players. Smoke ownership must change only rendering, never tracking rules. Camera/effects/settings must remain coherent at different speeds, car profiles and viewport sizes. Map objectives must be reachable with valid grades and no accidental invisible boundaries.

Track exact observed evidence in a verification report; tests alone cannot prove visual quality or network behavior. No completion claim until every backlog item and the new map is verified.
