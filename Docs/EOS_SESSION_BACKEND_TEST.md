# EOS guest session test and permission fix

Tested 2026-10-06 with the supplied Live deployment, Device ID identity and UE 5.8.3's bundled SDK. No Epic account sign-in or EAS application was used.

## Live results

**Policy retest passed at 2026-10-06 20:21 local time.** After the user saved a Custom policy with Sessions > managePlayers enabled and User required checked, the fresh live probe exited 0. Device ID login retained device A's PUID; Publish, RegisterGuest, Discover and Destroy all returned EOS_Success. Metadata verification confirmed capacity 2 and one open slot after host registration. Cleanup was confirmed.

Current evidence: [successful authentication/overall report](../Saved/EOSProof/SessionSmoke-PolicyRetest-20261006-202147.json), [successful session operations](../Saved/EOSProof/SessionSmoke-PolicyRetest-20261006-202147.json.sessions.json). The backend permission gate is satisfied. User required did not prevent this authenticated Connect Device ID flow; this result does not require adding an Epic account sign-in.

The table and permission-fix instructions below retain the earlier failure for diagnosis. No further policy change is required for this tested gate. Next: Unreal guest identity/transport integration, then real cross-device gameplay. Other session lifecycle operations remain untested.

| Operation | Result |
|---|---|
| Device ID login | EOS_Success; existing device A PUID retained |
| Publish public two-player session | EOS_Success |
| Register guest player | **EOS_ClientPolicyMissingAction** |
| Discover session by exact ID | EOS_Success |
| Read metadata | Correct ID, host address, capacity 2, open slots 2 |
| Delete test session | EOS_Success |

Registration failed, so two open slots are expected. A passing rerun must show one open slot after registering the host. Overall result is correctly false.

Evidence: [overall/authentication report](../Saved/EOSProof/SessionSmoke-Final.json), [session operations report](../Saved/EOSProof/SessionSmoke-Final.json.sessions.json). Three live runs reproduced the registration error and confirmed deletion; the final two also verified discovery and metadata.

## Setup side-chat instructions

1. In Epic Developer Portal, select CitixChase product `d699b76605144e959b9bd8f7cb68af69`, then Product Settings > Clients.
2. Locate client `xyza7891d7ABCRqNMKKFd25QNZDVz7tB` and inspect its assigned policy. A client named peer2peer does not establish which actions are enabled.
3. Under Sessions, enable the permission for **registering players in a session**, corresponding to `EOS_Sessions_RegisterPlayers`. If the preset cannot be edited, use a Custom policy retaining existing permissions and add this action. Inspect actual portal labels: this probe did not read the portal, so its exact checkbox label is not confirmed.
4. Preserve existing Device ID Connect, session creation/search/deletion permissions and the same client/environment. Do not add a player-facing Epic login or EAS application.
5. Save and verify that this client uses the updated policy. Review the later join/unregister/update/start/end permissions when implementing those operations; they have not yet been tested.
6. Return to the engineering chat and rerun below. If a fresh process still gets the same error immediately, allow time for the policy change to apply and verify the assignment.

The backend explicitly rejected the registration call for a missing policy action. The exact policy choice and checkbox require inspecting the user's portal.

## Rerun on device A

No second PC is needed for this backend check. In PowerShell:

```powershell
Set-Location 'E:\UnrealProjects\CitixChase'
cmd /c Tools\build_eos_session_smoke.cmd
if ($LASTEXITCODE -ne 0) { throw 'Session probe build failed' }
$sessionReport = 'Saved/EOSProof/SessionSmoke-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.json'
& Saved/EOSSessionProbe/CitixEOSSessionSmoke.exe Saved/EOSLocal/device-proof.ini $sessionReport 04a93fd9090a0d14480a41296fb2f8193e24ee5c32bab5c66c14890cc0a0419c
$sessionProbeExit = $LASTEXITCODE
Get-Content ($sessionReport + '.sessions.json')
Write-Host "Probe exit: $sessionProbeExit"
```

Expected: exit 0, RegisterGuest/Publish/Discover/Destroy all EOS_Success, metadata_verified and cleanup_confirmed true. The marker above belongs to device A; do not reuse it on another machine. The executable and matching DLL are in Saved/EOSSessionProbe. It reads the existing private INI without printing the secret and refuses to replace existing reports.

The probe attempts to delete every successfully published session even after a failure. If deletion ever fails, resolve cleanup using the recorded session ID before considering testing complete.

## Boundaries and next gate

This native probe tests backend permissions and exact-ID discovery. The advertised EOS address is metadata, not evidence of a bound socket. Public room browsing, cross-device joining, relay connectivity, Unreal identity integration and replicated gameplay remain unverified.

After registration passes, build the project-owned guest identity/Unreal transport adapter and test two physical PCs on different networks. Device B can use a packaged game without installing UE. Preserve current game admission checks and LAN under Advanced.

CitixEditor Win64 Development rebuilt successfully; all eight existing identity/comparison/portable-launch tests passed. Self-review improved specific-error diagnostics, discovery after the missing permission response, callback lifetime, existing-report protection, accurate login status after post-login failure, and cleanup. No gameplay/menu/network-default changes were made during this stage.

References: [Epic session modification](https://dev.epicgames.com/docs/epic-online-services/multiplayer/lobbies-and-sessions/sessions-interface/modify-a-session), [Epic session information](https://dev.epicgames.com/docs/epic-online-services/multiplayer/lobbies-and-sessions/sessions-interface/get-information-about-a-session). Exact API versions were read from the installed eos_sessions.h and eos_sessions_types.h.
