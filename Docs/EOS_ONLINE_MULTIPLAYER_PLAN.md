# CitixChase EOS online multiplayer plan

Updated: 2026-10-06. Supersedes the Epic-account-login design.

**Implementation update:** The user authorized full game integration after the identity and backend gates passed. Runtime EOSShared/SocketSubsystemEOS, a project guest Connect/Sessions service, a project NetDriverEOS bridge, online primary controls and Advanced LAN controls are now implemented. Editor/game builds, 16 Unreal tests, 14 Python tests, actual packaged EOS hosting/discovery/cleanup and rendered local LAN gameplay passed. Cross-device relay gameplay is awaiting physical test receipts. Older proof-only scope statements below are historical; current status and instructions are in [EOS_GAME_IMPLEMENTATION.md](EOS_GAME_IMPLEMENTATION.md) and [EOS_ONLINE_PLAYTEST.md](EOS_ONLINE_PLAYTEST.md).

## Agreed direction

Internet play is the main path: **Host Online Game** and **Find Online Games**. **Advanced** retains **Host LAN Game**, **Find Nearby Games**, **Host IP** and **Join IP**. Players never need an Epic account or a sign-in screen. Online performs automatic EOS Connect **Device ID authentication** internally; LAN remains offline and independent.

Preserve the two-driver chase, Runner/Chaser roles, city verification, Ready, round swapping, rematches, driving, Smoke, Ice Wave, gates, physics, graphics preferences and art direction. Do not modify the original Citix tree or overwrite existing packages/deployment.

The immediate authorized milestone is **Device ID proof before online rooms**. See [proof instructions and results](EOS_DEVICE_ID_PROOF.md) and [updated setup handoff](EOS_SETUP_SIDE_CHAT_GUIDE.md).

## Identity proof gate

**Passed on 2026-10-06.** Device A and the user's device B report have distinct LoggedIn PUIDs in the same environment/SDK, and device A retained its PUID across restart. The report checker returned PASS, exit 0. See the linked proof document for evidence and provenance. Next: identity/session/transport bridge.

Required evidence:

1. Device A logs in inside UE 5.8 without EAS/account authentication and reports a valid LoggedIn PUID.
2. A separate physical Windows device uses the same product/sandbox/deployment and receives a different valid LoggedIn PUID.
3. Device A retains its PUID across process restart.
4. Failed login, same-device runs and different environments cannot pass the comparison.

The second device has no UE installation. A portable harness uses the same C++ Connect sequence and the exact SDK DLL bundled with this UE 5.8 installation. This verifies the underlying identity behavior; it does not establish Unreal OSS registration, gameplay transport, sessions or internet matches.

## Supplied environment

| Setting | Value |
|---|---|
| Product ID | d699b76605144e959b9bd8f7cb68af69 |
| Sandbox ID | 682f55bcddbc45489f2040275ee212d1 |
| Deployment ID | bf5f9d3a6c1b406188205ce4dd7fc185 (user identified as Live) |
| Peer2Peer client | xyza7891d7ABCRqNMKKFd25QNZDVz7tB |
| Client Secret | Local config and ready kit (user-authorized); excluded from reports/documents |

Identity tests can create product users in the supplied Live deployment. The probe never creates rooms, changes portal policy, or resets/deletes Device ID credentials. Prefer a separate development deployment for ongoing development, but do not change the supplied environment while proving this milestone.

## Current source and engine findings

Active root: E:\UnrealProjects\CitixChase. Project/module/executable names remain Citix. Installed engine is **5.8.3-58210709**; its bundled EOS SDK is **1.19.1.2-53289219**.

| File | Responsibility |
|---|---|
| Citix.uproject | NULL retained; proof enables EOSShared for Editor only. Later choose runtime plugins after the identity bridge gate. |
| Source/Citix/Citix.Build.cs | Proof adds editor-only EOSShared/EOSSDK dependencies. |
| Config/DefaultEngine.ini | Default service remains NULL; unchanged for this proof. Preserve graphics/physics/map/redirect settings. |
| Source/Citix/Network/CitixSessionSubsystem.h/.cpp | Existing NULL LAN lifecycle, room metadata, retry/cancel/leave and protocol/network checks. Extend instead of replacing it. |
| Source/Citix/Chase/CitixChaseLobbyWidget.h/.cpp | Existing nearby browser, Advanced address entry and connected Ready panel. |
| Source/Citix/Chase/CitixChaseGameMode.h/.cpp | Capacity/phase/version admission and authoritative city identity/layout. |
| Source/Citix/Player/CitixDrivingPlayerController.cpp | Host/join wrappers, shortcuts and world-ready/Ready flow. |
| Source/Citix/Network/CitixSessionTest.cpp | Existing LAN regression tests. |
| Source/Citix/Network/CitixEOSDeviceProofCore.h | Shared raw EOS Connect proof sequence. |
| Source/Citix/Editor/CitixEOSDeviceProofCommandlet.h/.cpp | Opt-in UE headless proof; no gameplay or asset writes. |
| Tools/eos_device_proof_main.cpp | Portable Windows harness of the same sequence. |
| Tools/run_eos_device_proof.ps1 | Launches either harness; computes a hashed machine/profile test marker. |
| Tools/compare_eos_identity_reports.py | Checks successful identities, environment equality, distinct markers/PUIDs and restart persistence. |

## Connect implementation and limitations

The installed OSS EOS has no automatic CreateDeviceId helper. Its desktop external-Connect path does not populate UserLoginInfo because ADD_USER_LOGIN_INFO defaults false. Device ID requires that structure and a display name. Normal no-EAS refresh also expects platform credentials. A naive IOnlineIdentity Login("deviceid") is insufficient.

The proof calls the engine-bundled SDK:

CreateDeviceId -> Success or DuplicateNotAllowed -> Connect Login with DEVICEID_ACCESS_TOKEN and UserLoginInfo -> Success, or InvalidUser -> CreateUser using the continuance token -> valid PUID with LoggedIn status.

DuplicateNotAllowed means reuse the existing device credential. Never delete it merely to obtain a second test identity. Guest identities belong to a device/local OS profile; they do not provide account-based recovery or cross-device progression. Display names are informational and may duplicate. [Official Connect reference](https://dev.epicgames.com/docs/epic-online-services/eos-fundamentals/connect-interface)

This Game Services path does not use EAS Auth, an EAS application, Dev Auth Tool or an Epic social overlay. Portal configuration/permissions and applicable EOS agreements still need to match the features used.

## Next gate: connect identity to game sessions and transport

A raw SDK PUID is not automatically an IOnlineIdentity user or local-player FUniqueNetIdRepl. Keep the existing room-manager public interface and replicated lobby, then investigate the smallest supported bridge:

| Candidate | Direction |
|---|---|
| Project-owned Connect adapter, direct EOS Sessions, public SocketSubsystemEOS utilities | First investigation. Reuses EOSShared without changing installed engine files; deliberately supplies platform/PUID/session identity to transport. |
| Narrow project plugin implementing required OSS identity support | Fallback if direct integration cannot provide coherent per-world identity, admission and transport. Must support user registration, guest refresh and cleanup. |

Do not migrate the game's session API to Unreal Online Services simply because that framework accepts Device ID tokens. Such a migration needs its own design and device-creation/transport proof.

Bridge acceptance: two UE game processes exchange replicated data using the authenticated PUID; online admission validates that identity; per-world platform selection is correct; auth expiry refreshes silently; offline NULL/IP LAN still works; cleanup leaves no stale callbacks/platform handles.

## Implementation sequence after proof

### 1. Automatic authentication and mode lifecycle

- [ ] Add Online/LAN mode and automatic Connecting/Connected/Error service state. No sign-in/sign-out buttons or account screen.
- [ ] Authenticate when online actions need it; LAN never waits for EOS. Failed authentication offers Retry and Advanced LAN.
- [ ] Implement auth-expiration notification, silent Device ID re-login and auth-loss cleanup. The short proof does not implement a persistent production auth service.
- [ ] Resolve platform/PUID per world. Capture provider and operation generation in callbacks; ignore obsolete completions and clean up late successful operations.
- [ ] Clear old search results when switching modes and complete cleanup before allowing another operation.

### 2. Two-player online rooms and transport

- [ ] Implement create/search/join/update/destroy through the selected bridge, keeping NULL discovery for LAN.
- [ ] Use two public slots and public discovery. Initial rooms have no private password/code, presence or lobby voice. A room name is a label.
- [ ] Reuse CITIX_GAME, CITIX_ROOM, CITIX_PROTOCOL, CITIX_NETWORK, CITIX_PLAYERS and CITIX_PLAYING metadata. Match bucket/game filters and field types on create/search.
- [ ] Validate protocol/network version and city identity independently. Enforce strict online admission when compatibility fields are missing.
- [ ] Verify actual EOS player registration/unregistration and backend slot accounting; a local PlayerArray/NumOpenPublicConnections edit is insufficient.
- [ ] Reject third/simultaneous joins, incompatible builds and mid-match entry. Coalesce/observe backend updates and reopen eligible rooms when returning to lobby.
- [ ] Preserve resolved EOS URLs before safely adding compatibility options. LAN uses ?listen?bIsLanMatch and existing address validation; online removes inherited LAN flags/custom ports.
- [ ] Use current /Script/SocketSubsystemEOS.NetDriverEOS with IP fallback, preserving other driver definitions. Installed 5.8 deprecates NetDriverEOSBase and bIsUsingP2PSockets.
- [ ] Verify the PUID/platform used by socket utilities, parent PreLogin and FUniqueNetIdRepl. Never disable online identity validation globally to repair LAN.

### 3. Menu and recovery

- [ ] Main section: PLAY ONLINE, room name, Host Online Game, Find Online Games. Show one clear automatic-connection progress message with Cancel/Retry.
- [ ] Advanced: LAN / SAME NETWORK, complete LAN host/browser and existing IP controls. Keep online and nearby lists separate.
- [ ] Results show room name, occupancy and Join/Full/In match/Different version. Do not invent gameplay ping from search duration or imply names are verified Epic accounts.
- [ ] Both modes use the existing city verification, Ready and rematch flow with an ONLINE/LAN label. EOS Join success alone never enables Ready.
- [ ] Audit shortcuts, controller wrappers, UI probes and all status/error copy for mode routing.
- [ ] Leave returns to the previous mode after cleanup. Host exit ends the match and returns the client to a recoverable menu.

### 4. Package and prove internet play

- [ ] Preserve existing PackageLAN and deployed builds; produce a new EOS development package.
- [ ] Run current LAN/chase automation plus focused lifecycle/admission tests.
- [ ] Use identical full packages on two physical PCs and different networks, without port forwarding or a LAN VPN. Verify EOS transport and relay fallback. [Relay behavior](https://dev.epicgames.com/docs/en-US/api-ref/enums/eos-e-relay-control)
- [ ] Play both roles, Smoke/Ice Wave/gates, full match, rematch, leave/rehost. Assess driving at 80-150 ms RTT and modest loss.
- [ ] Test offline two-PC LAN in the combined package, auth failure, stale/full/incompatible rooms, host/client loss and Online -> LAN -> Online.
- [ ] Verify shipping runtime/config staging and automatic guest login on a clean device. No EAS/Dev Auth Tool dependency is part of the chosen flow.

EOS discovery/P2P does not run the city simulation. The host player's PC remains the authoritative listen server. Dedicated hosting, host migration, account recovery, friends invitations, queues and anti-cheat are deferred. [Epic OSS reference](https://dev.epicgames.com/documentation/en-us/unreal-engine/online-subsystem-eos-plugin-in-unreal-engine)

## Completion and product review

The identity milestone is complete only when real second-device evidence passes alongside device A restart persistence. Online multiplayer requires the later bridge and cross-network game tests. Never substitute synthetic reports, same-PC runs or source inspection for those observations.

After online implementation, perform up to three review/improvement cycles. Score online entry clarity, LAN discoverability, connection recovery, chase continuity and menu coherence honestly. Fix the largest immediately visible weakness first; inspect menu-to-rematch as one experience. Refine feedback and navigation before adding features.
