# CitixChase online game test

The game now implements guest EOS sessions and Unreal EOS transport. Online hosting/finding is the primary lobby flow. Advanced contains LAN hosting/discovery and direct IP controls. There is no Epic account sign-in.

## Verified locally

- Editor build and 16 automation tests passed, including existing chase/physics/graphics/LAN tests and new online default/transport URL checks.
- Real game process logged into EOS, published/registered a room and opened an Unreal EOS P2P listener. Its occupancy update succeeded.
- A separate game process discovered that room with one player and correct compatibility metadata. Same-device identity results are excluded from joining yourself.
- Leaving deleted the online session and returned to a standalone menu; the host probe receipt confirmed cleanup.
- The final Development package built/cooked/staged/archived successfully. A fresh extracted ZIP retained exact executable/SDK hashes, excludes local Saved/Reports, and passed forced-relay-configured EOS hosting and cleanup. This proves packaging/listening, not a relayed peer connection.
- Two rendered local LAN instances used Advanced > Host LAN / Find Nearby, joined, readied and entered a replicated pursuit with two cars. This verifies local LAN regression, not physical two-PC LAN.

Evidence: Saved/Logs/EOSOnline-Host1.log, EOSOnline-Search1.log, EOSOnline-Automation2.log, ChaseMP_eos-lan-render_Host.log and ChaseMP_eos-lan-render_A.log. Receipts: Saved/EOSProof/Game-OnlineHost1.json and Game-OnlineSearch1.json.

## Two-PC relay proof

**Passed on 2026-10-06.** The live host receipt at `PackageOnline/Reports/DeviceA-20261006-212656/Game-DeviceA.json` and user-supplied guest receipt saved as `Saved/EOSProof/Game-DeviceB-20261006-Relay.json` passed `compare_eos_game_reports.py` (exit 0). The user pasted the same Join receipt twice; the Host receipt was obtained independently from this device.

Both report session `4d47f65336364df78fc181afa54666ba`, two players, city seed 1337/hash -1809114713, reciprocal distinct guest identities, EOS relay and replicated pursuit. The host log records the pursuit proof at 21:27:33 Denver, then successful remote-player unregister and reconnect/register. The attached Device B log also confirms discovery and relay connection on its later join attempt; that excerpt ends before pursuit and is not a separate pursuit proof.

This verifies the guest online room and replicated chase flow on two physical devices. Full driving/combat/rematch testing and long-duration login refresh remain manual coverage items. Receipts are captured during play, so `cleanup_confirmed:false` is expected; host-side remote-player unregister succeeded, while complete room deletion for this physical test has not yet been recorded.

Distribution: [CitixChase-Online-Test.zip](../CitixChase-Online-Test.zip), 432.5 MiB. ZIP SHA256: E98CBAB928A0EF1853682129B63409A24CCD9A9680C8009342C5C41E3D11DA63. The folder also contains a bundled x64 Microsoft C++ runtime installer if the second PC needs it.

Use the complete PackageOnline distribution on both physical Windows PCs. UE does not need to be installed on device B. Extract the whole ZIP; leave the Windows, Citix and Engine folders together. Prefer different internet connections (for example home internet and phone hotspot).

1. Device A: run **Test Online Host.cmd**. It creates CitixOnlineTest automatically and waits up to ten minutes for the other PC.
2. Device B: run **Test Online Join.cmd**. It searches for CitixOnlineTest and joins automatically. Start only one test host in the deployment to avoid ambiguous room names.
3. Both tests force EOS relay. The game verifies city identity and replicated layout, then sends the normal Ready interaction. Passing requires two players, EOS transport, an observed relay peer, two cars and at least five seconds in pursuit; the host also requires both EOS players registered.
4. On success the test keeps the game open so both players can drive. Check that each player sees the other car moving, Smoke/Ice Wave work, and ordinary round/rematch flow works. Automatic proof covers transport/city/Ready/pursuit, not every combat interaction.
5. Return **Game-DeviceA.json** from the host and **Game-DeviceB.json** from the guest. The launchers create a new per-run folder inside **Reports**, beside the Windows folder. Logs are in the same folder. Return Online-DeviceA.log and Online-DeviceB.log for any failure.
6. Leave the room through the menu after testing. Confirm the host's log reports `Leave online room: EOS_Success`. The success receipt is written while playing and does not itself prove end-of-test cleanup.

Engineering comparison:

```powershell
python Tools/compare_eos_game_reports.py path/to/Game-DeviceA.json path/to/Game-DeviceB.json
```

Expected: PASS, exit 0. It checks different identities, mutually observed transport peers, same session/city, two players, EOS transport and relay evidence on both sides. This requires honest physical runs; JSON checks cannot attest hardware or substitute for running the game.

## Ordinary play and LAN

Run Play CitixChase.cmd. Host Online creates a public room; Find Online Games searches this deployment. Both players must use the same game build. Host keeps the game running. Ready starts the chase once both cities are verified.

LAN is independent of EOS authentication. Open Advanced, choose Host LAN or Find Nearby on the same network. Host IP/Join IP remain available there. A fresh LAN-only run should not emit any `[CitixOnline] Guest login` operation, even though the EOS SDK plugin is loaded with the executable.

## Portal troubleshooting

The saved Custom Sessions > managePlayers policy already passed host registration. No additional portal change has been demonstrated necessary yet. If a new backend operation returns EOS_ClientPolicyMissingAction, identify the exact preceding `[CitixOnline]` operation and enable only the corresponding action in this client's policy. Do not add an Epic login or change deployment to address a transport failure.

Unreal `EOS:PUID` socket addresses are wrapped as `[EOS:PUID]` for FURL travel parsing. Credentials are packaged in runtime configuration as authorized. This implementation retains NULL gameplay net IDs and validates EOS transport identities separately for membership; it does not weaken the game's version, capacity, phase or city checks.

## Self-review record

Initial review: flow clarity 7/10, lifecycle reliability 6/10, preservation 8/10, online verification 5/10. Highest-impact gaps were missing actual peer evidence, confusing LAN results under online headings, and refresh/error handling. Improvements add explicit results/provider labels, deferred silent refresh, preserved specific version/full/in-progress errors, economical occupancy updates and a relay/city/Ready/pursuit receipt. Final review: flow clarity 8/10, lifecycle reliability 7/10, preservation 8/10, core online verification 9/10. Cross-device relay pursuit is now proven; remaining coverage concerns are long-duration refresh and a complete manual round/rematch test. Further feature additions are unnecessary for this milestone.
