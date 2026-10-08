# CitixChase EOS Device ID setup handoff

Updated: 2026-10-06. **No player account login is required.** This replaces the earlier EAS application/brand-review setup path.

**Current checkpoint: Device ID proof passed.** The user returned the successful device B report; comparison confirmed distinct device markers/PUIDs, matching environment/SDK and device A restart persistence. Portal/device setup does not need repeating. The next implementation gate is Unreal identity/session/transport integration, not additional EAS setup. The instructions below remain available for reproducing the identity test.

## Copy into the side chat

    Help me complete the CitixChase EOS Device ID proof.
    Read these files first:
    E:\UnrealProjects\CitixChase\Docs\EOS_DEVICE_ID_PROOF.md
    E:\UnrealProjects\CitixChase\Docs\EOS_ONLINE_MULTIPLAYER_PLAN.md

    The active project is E:\UnrealProjects\CitixChase\Citix.uproject,
    UE 5.8.3; module/executable Citix. Players must not sign into Epic.
    We use automatic EOS Connect Device ID authentication, no EAS
    application, no Epic social overlay and no Dev Auth Tool.
    Online rooms are deferred until the identity proof is complete.

    Product: d699b76605144e959b9bd8f7cb68af69
    Sandbox: 682f55bcddbc45489f2040275ee212d1
    Live deployment supplied by user: bf5f9d3a6c1b406188205ce4dd7fc185
    Peer2Peer client: xyza7891d7ABCRqNMKKFd25QNZDVz7tB
    Private local config:
    E:\UnrealProjects\CitixChase\Saved\EOSLocal\device-proof.ini
    Never print its ClientSecret or copy it into a guide/log.

    Device A has already logged in inside UE and retained its PUID
    across restart. A portable harness using the exact UE-bundled SDK
    is prepared for device B, which has no UE installation.
    Read the current evidence report before repeating tests.
    Guide me through transferring the kit privately, running the ready-configured executable on device B, and returning only the JSON report.
    Use the comparison checker to verify distinct device markers/PUIDs,
    identical environment/SDK and stable device A restart.

    Do not create an EAS application, submit brand review, reset/delete
    Device ID credentials, invite other people, change portal policy,
    publish, or implement online rooms for this proof task.
    If a portal permission error occurs, inspect the actual result and
    client policy before suggesting changes. Ask for explicit instructions
    before making those external changes.

    Report separately: UE authentication, same-device restart,
    portable local authentication, second-device identity proof,
    Unreal session/transport integration and internet gameplay.
    A raw Connect PUID does not prove IOnlineIdentity/NetDriver integration.

## Prerequisites already supplied

Product, sandbox, deployment, client ID and Client Secret were supplied. The secret is stored only in the private local config. The ready-to-run portable ZIP now contains the Client Secret at the user's explicit request.

No EAS application was created; that is intentional. This flow uses EOS Game Services Connect, not EAS Auth/accounts/social. [Official Connect interface](https://dev.epicgames.com/docs/epic-online-services/eos-fundamentals/connect-interface)

## Remaining setup and test steps

**Game integration update:** Runtime guest EOS play is implemented and the portable game ZIP is ready. No additional portal denial has appeared in local game hosting/discovery/cleanup. Follow [EOS_ONLINE_PLAYTEST.md](EOS_ONLINE_PLAYTEST.md) for the remaining physical two-PC relay match test; the current goal remains unverified until that evidence is available.

**Current status (2026-10-06):** The two-device identity gate and session backend gate passed. After the user enabled Sessions > managePlayers in a Custom policy with User required checked, the live retest successfully authenticated via Device ID, published a session, registered the guest, discovered correct metadata and deleted the session (exit 0). See [EOS_SESSION_BACKEND_TEST.md](EOS_SESSION_BACKEND_TEST.md) for evidence. No further portal change is required for these tested operations. Next is Unreal guest identity/transport integration and cross-device gameplay. The checklist below records the earlier identity stage; do not repeat that completed gate.

1. Read [EOS_DEVICE_ID_PROOF.md](EOS_DEVICE_ID_PROOF.md) for current results.
2. Use a **second physical Windows PC** with internet access. UE 5.8 is not required for the portable harness.
3. Transfer [EOSDeviceProofKit-Ready.zip](../EOSDeviceProofKit-Ready.zip) privately and extract the complete folder. Do not add device A's credential store, OS profile, Reports folder or private config to a public upload.
4. The ready kit has complete client configuration. Keep the files together and retain the supplied IDs.
5. Double-click **CitixEOSDeviceProof.exe** after extracting all files. It should report SUCCESS and produce a 32-character PUID. It waits for Enter before closing. Missing credentials/access/network errors are failures, not identity proof.
6. Return only the new JSON report from the kit's Reports folder; the config is not needed.
7. Compare it with the recorded device A first/restart reports. Two runs on device A must be rejected as a two-device proof.
8. Record the outcome and stop at the identity gate. Begin online room work only after the user continues that stage.

The hashed device marker is an observation aid, not hardware attestation. Cloned OS profiles, edited reports or copied credential stores invalidate the intended physical-device test.

## If something fails

| Stage | First check |
|---|---|
| Config | ClientSecret filled locally, correct INI section/IDs and readable file |
| SDKInitialize / PlatformCreate | Complete portable folder, correct DLL/architecture and credentials |
| CreateDeviceId | SDK result; DuplicateNotAllowed is expected reuse and not a login failure |
| ConnectLogin / CreateUser | Client policy, product/environment and network access; inspect exact result |
| Compare | Success/LoggedIn, valid PUID, same environment/SDK, distinct devices/PUIDs and stable restart |

Do not delete device credentials to force a different PUID. Do not weaken unrelated permissions or add account login to address a specific error.

## Handoff report

    Device A UE report:
    Device A restart report:
    Device B physical machine test performed: yes/no
    Device B portable report:
    Environment/SDK match:
    Distinct device markers/PUIDs:
    Stable device A PUID:
    Comparison checker result:
    Any blocker:
    Identity proof complete: yes/no
    Unreal room/transport integration tested: no (unless separately implemented)
    Internet match tested: no (unless separately implemented)

Portal terminology may change. Use [Epic Developer Portal concepts](https://dev.epicgames.com/docs/dev-portal/dev-portal-intro?lang=en-US) when investigating actual client/environment settings. Keep the supplied Live deployment distinct from any new development environment; do not compare PUID tests from different environments.
