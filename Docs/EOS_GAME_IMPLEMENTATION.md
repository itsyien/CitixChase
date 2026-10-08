# Guest online multiplayer implementation

Goal: finish automatic Device ID internet play in the actual CitixChase game, with LAN in Advanced. The user authorized continuous implementation on 2026-10-06. Prior identity and backend permission gates passed.

Architecture: retain NULL for Unreal's existing gameplay identity and LAN sessions. A project-owned EOS service owns a managed SDK platform, Connect guest login/refresh and backend sessions. A project NetDriverEOS subclass selects the guest socket subsystem per game instance; Unreal's existing EOS transport carries actor replication. Remote player membership comes from the authenticated EOS transport address, not a caller-supplied PUID. No installed-engine edits or Epic sign-in UI.

Transport addresses must follow this installed UE 5.8 implementation: EOS:PUID. Earlier portable metadata with socket name/port is not a valid Unreal join URL.

Execution: inline; preserve original project, existing packages, chase gameplay, city verification, physics and graphics. Back up changed files before editing.

- [x] Add meaningful online/LAN mode tests and transport URL admission tests; observe failure before implementation. EOSOnline-Red.log records the primary-flow failure; final automation passes.
- [x] Add guest service with managed platform lifetime, silent Connect login/refresh, session create/register/search/join/update/destroy, cancellation and callback safety. Enable runtime EOSShared/SocketSubsystemEOS and configured credentials. Long-duration refresh and cross-device join still require live coverage.
- [x] Integrate provider selection in existing manager; online default, explicit LAN actions, correct travel/leave/error lifecycle.
- [x] Update lobby primary buttons and Advanced LAN controls. Preserve connected Ready/rematch flow and polish status/recovery feedback. Packaged screenshots inspected at 1280x720.
- [x] Build editor/game, run automation and live in-game host/search/leave tests. Verify offline LAN still works and no false claim from raw backend tests. 16 Unreal tests and 14 Python tests pass; rendered LAN instances reached pursuit; packaged EOS listener/cleanup passed.
- [x] Package a separate online test build and verify actual cross-device/relay gameplay. On 2026-10-06, host and Device B receipts passed comparison: distinct guest identities, mutual EOS relay peers, matching session/city, two players and replicated pursuit. Prior deployed packages were preserved.

The core online multiplayer goal is verified by real cross-device gameplay evidence, not just green builds/backend calls. Long-duration authentication refresh, every combat interaction and full round/rematch coverage remain broader playtest items. If the portal denies a new action, record exact function/result and a focused setup change.

Distribution: [CitixChase-Online-Test.zip](../CitixChase-Online-Test.zip), 53 runtime/launcher/prerequisite files, 432.5 MiB; excludes local Saved/Reports and debug symbols. Runtime executable/SDK hashes are in PackageOnline/online-build.json. See [online playtest instructions](EOS_ONLINE_PLAYTEST.md). No further portal denial has appeared in verified operations.
