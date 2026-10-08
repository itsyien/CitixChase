# CitixChase Device ID proof

Updated: 2026-10-06. **Device ID identity proof passed: distinct device A/B PUIDs, matching environment/SDK, and stable device A restart. Online rooms are not implemented.**

## What is ready

- UE 5.8.3 commandlet in the active CitixChase project.
- Portable Windows x64 kit using the exact engine-bundled EOS SDK **1.19.1.2-53289219**, with the same C++ authentication sequence. No UE install, Epic account or Dev Auth Tool needed on device B.
- Local credential config with the supplied environment/client. Client Secret is included in the ready-to-run kit at the user's request; it remains excluded from reports and documents.
- Report comparison script and six automated tests.

## Live evidence

| Run | Outcome |
|---|---|
| [First UE login](../Saved/EOSProof/DeviceA-First.json) | Success, LoggedIn, PUID **00026d2988b442018995aab726cb0d7e** |
| [Verified UE restart](../Saved/EOSProof/DeviceA-Restart-Verified.json) | Success, same PUID; CreateDeviceId returned expected EOS_DuplicateNotAllowed; process exit 0 |
| [Portable login on device A](../Saved/EOSProof/DeviceA-Portable.json) | Success, same PUID and SDK; validates shared behavior, not a second device |
| [Device B report supplied by user](../Saved/EOSProof/DeviceB.json) | Success, LoggedIn, distinct PUID **00021767c6d24a038e600e201e4ce91e** and distinct device/profile marker |

The comparison checker returned **PASS**, exit 0, against device A's first login, device B's submitted result and device A's verified restart. Device B evidence comes from the user's returned portable report; the agent did not directly operate that physical device.

Product: d699b76605144e959b9bd8f7cb68af69. Sandbox: 682f55bcddbc45489f2040275ee212d1. Supplied Live deployment: bf5f9d3a6c1b406188205ce4dd7fc185.

The first observed Connect login returned EOS_Success directly. The InvalidUser/CreateUser branch is implemented but was not exercised by this observed identity. An SDK login/create-user success counts only if the returned PUID is valid and GetLoginStatus is LoggedIn.

No Epic account login, EOS Auth call, room creation, asset modification or credential-reset call was used. The proof can create a guest product user where needed in the supplied deployment. It intentionally retains the local Device ID for restart testing.

## Run device A again inside UE

Use a fresh report filename:

```powershell
& 'E:\UnrealProjects\CitixChase\Tools\run_eos_device_proof.ps1' `
  -Mode Unreal `
  -Report 'E:\UnrealProjects\CitixChase\Saved\EOSProof\DeviceA-New.json'
```

The private config defaults to Saved/EOSLocal/device-proof.ini. Do not pass Client Secret on a command line or print the config. The commandlet reads it locally and writes only non-secret result fields. Missing settings fail before authentication; timeout is 60 seconds. Exit 0 means successful validated login, 2 means config/auth failure, 3 means report-writing failure.

On repeat runs EOS logs DuplicateNotAllowed as an Error even though reusing the credential is expected. The commandlet explicitly uses its validated result for the process exit code; the log retains the SDK message for diagnosis.

## Run device B without Unreal Engine

1. Copy [EOSDeviceProofKit-Ready.zip](../EOSDeviceProofKit-Ready.zip) privately to a **second physical Windows PC** and extract the entire kit to a writable folder.
2. Configuration, including the Client Secret, is already filled in. Keep the extracted files together.
3. Double-click **CitixEOSDeviceProof.exe** (or Run Device Proof.cmd). It now runs directly and waits for Enter before closing. Internet access is required.
4. Check that the console says **SUCCESS: Device ID login succeeded** and shows a PUID. The JSON report is saved under the extracted kit's Reports folder.
5. Return only that JSON report, not the config file. Its PUID should differ from device A's. Do not copy device A's OS profile/EOS credential store to the second PC.

The executable uses a static C++ runtime, so installing Visual Studio/UE is unnecessary. The EOS DLL must remain beside the executable. All kit files are required except this explanation can be read independently.

## Compare actual results

Place device B's JSON at Saved/EOSProof/DeviceB.json, then run:

```powershell
python 'E:\UnrealProjects\CitixChase\Tools\compare_eos_identity_reports.py' `
  'E:\UnrealProjects\CitixChase\Saved\EOSProof\DeviceA-First.json' `
  'E:\UnrealProjects\CitixChase\Saved\EOSProof\DeviceB.json' `
  --restart-a 'E:\UnrealProjects\CitixChase\Saved\EOSProof\DeviceA-Restart-Verified.json'
```

Expected: PASS, exit 0. It requires successful DeviceId/LoggedIn records, valid PUIDs, identical environment/SDK, different device/profile markers, different PUIDs, and persistence on device A. Missing/invalid/mismatched reports fail.

Device markers are hashes of the OS machine marker plus user SID. They avoid exposing raw machine/profile identifiers, but are **not hardware attestation**. The test requires honest runs on separate physical machines; cloned/edited reports cannot establish that fact. Two processes on device A correctly fail the two-device comparison.

## What this does not prove

The portable kit runs the UE-bundled SDK, not Unreal Engine itself. The commandlet also uses raw EOS Connect, not an IOnlineIdentity login. A raw PUID does not automatically register a local player with OSS EOS or connect Unreal's session/transport pipeline. That integration is the next engineering gate, followed by room discovery and cross-network gameplay.

The probe is deliberately short-lived. Auth expiry notifications, silent refresh, persistent UI state, membership registration, game network identity validation and P2P gameplay still need implementation.

## Verification and review

- Ready-kit revision: direct executable startup now resolves its own folder, waits for Enter on success/failure, and writes startup-error.txt for incomplete extraction. No PowerShell script is needed for the portable run.
- The rebuilt ZIP includes the supplied secret by explicit user request. A fresh extracted copy automatically authenticated successfully, retained the observed device A PUID, waited for Enter and exited 0. ZIP integrity and manifest hashes passed.
- Two new launcher tests passed alongside the six comparison tests, including a folder with spaces/non-ASCII characters and an unrelated working directory.
- CitixEditor Win64 Development builds with the new commandlet.
- Portable native probe compiles against the installed engine SDK, with an exact-copy DLL hash check.
- Six comparison tests passed, including distinct successful users, same-device rejection, same-PUID rejection, restart identity change, environment mismatch and invalid/failed login.
- Existing Citix/CitixChase automation: **14 tests performed, 14 succeeded**. [Regression log](../Saved/Logs/EOSProof-Regression.log)
- Missing-secret runs generated explicit failure reports in both harnesses.
- Real UE login and restart returned the same LoggedIn PUID; local portable login agreed.
- Product review fixed direct-executable startup and persistent error feedback, misleading restart exit status, removed the UE installation requirement on device B, and supported paths containing non-ASCII characters. Reports are non-secret; portable distribution includes the supplied client secret as explicitly requested.

The identity gate is now satisfied by the observed device A runs and the user's device B report. The next engineering gate is registering the guest identity with Unreal's session/transport path before implementing online room discovery. Existing LAN networking/configuration and deployed packages were retained; offline physical two-PC LAN was not re-tested for this proof-only change.

## Files and boundaries

Changes: editor-only EOSShared plugin entry; editor-only EOSShared/EOSSDK Build.cs dependencies; new commandlet/core, tools, portable kit and updated EOS documents. DefaultEngine.ini, session manager, lobby, gameplay rules, assets and existing game packages/deployment were not changed.

Pre-change copies of modified existing files are under Saved/Backups/eos-device-proof-20261006-192809. Do not treat the SDK-created local device credential as a disposable test file; deleting it can lose an unlinked guest identity.

See the [updated room plan](EOS_ONLINE_MULTIPLAYER_PLAN.md) and [side-chat setup handoff](EOS_SETUP_SIDE_CHAT_GUIDE.md). [Official EOS Connect reference](https://dev.epicgames.com/docs/epic-online-services/eos-fundamentals/connect-interface)
