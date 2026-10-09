# Host LAN / Host Online crash fix

2026-10-07, v1.0 hotfix.

## Root cause

The initial menu builds a city and loads the new bush mesh. Host LAN and Host Online both replace that world through ServerTravel. Unreal garbage collection destroys the old city components and unloads assets they alone retained. The process-wide FCitixSurfaceLibrary MeshCache is a static map, not a reflected UObject property: its TObjectPtr values do not by themselves keep assets alive. The bush mesh was collected while its pointer remained cached. The next city rebuild reused this stale pointer in ACitixCityChunk::GetOrCreateComponent, causing an access violation.

Four installed Shipping crash reports at 18:32 matched the user's failure. The actual LAN lobby button reproduced it in an editor game, with the stack through GetOrCreateComponent -> FinishChunks -> BuildCity -> GenerateCity -> BeginPlay after travel. HostCrash-Repro.log records that failure. This was introduced by the roadside mesh cache. The previous release validation checked startup and isolated fixtures, and missed hosting map travel.

## Fix

Retain the successfully loaded bush mesh with AddToRoot before inserting it into the process-wide cache. This is one constant asset, matching the lifetime of the cache and the existing rooted material cache. A failed load is not inserted. The fix leaves collision, foliage art, drag, progressive drift steering, airflow and Ice Wave behavior intact.

## Regression evidence

- HostCrash-Red.log: CacheSurvivesHostTravel failed because the cached bush died during explicit garbage collection. Exit -1.
- HostCrash-Green.log: all 22 automation tests passed, including the new regression and existing roadside, drift, Ice Wave, LAN, online and graphics tests. Exit 0.
- The opt-in CitixHostTravelProbe uses the existing CitixLobbyButtons harness to activate the real lobby buttons. Its receipt requires a listen-server world and the local player's verified city. It can leave and rehost within the same process to exercise repeated collection/travel. The flags do not run in normal play.

- Docs/evidence/host-travel/ShippingLANFixed.json: the installed Shipping executable passed three consecutive real Host LAN / leave / rehost cycles. Each receipt requires a listen server and verified city. Final cycle count: 3.
- Docs/evidence/host-travel/ShippingOnlineFixed.json: the same installed executable passed three consecutive real Host Online / leave / rehost cycles through EOS. Final cycle count: 3; online=true and city_verified=true. The final hosted room screenshots were visually inspected in both modes.
- HostCrash-Package-v1.0.log: final Shipping BuildCookRun succeeded, exit 0.
- HostCrash-Deployment.txt: all 34 installed release files and SHA-256 hashes match PackageRelease-v1.0. Destination remains D:/_YienStudio/CitixChase, version v1.0.
- No new installed crash directory appeared during either packaged proof; the newest remained the original 18:32:42 report.

The previous installed package is backed up at Saved/Backups/deployment-before-hostfix-20261007-183852. The modified source's pre-fix copy is recorded in Saved/Logs/host-crash-source-backup.txt.

## Self-review

The highest-impact quality is reliable room entry after replacing the menu world. The regression now tests the missing asset-lifetime boundary; packaged verification must additionally exercise both real Host buttons, rather than stopping at startup. The fix retains one mesh and adds no per-frame work. Test fixtures remain opt-in. This hotfix does not claim a new physical two-device relay or full combat/rematch playthrough.

Final review: host entry reliability 9/10, regression coverage 8/10, preservation of existing gameplay/style 9/10, simplicity 9/10. The highest-impact weakness was missing map-travel coverage; forced-collection regression plus six actual packaged host cycles now cover that boundary. Remaining coverage is longer multiplayer play and subjective driving feedback, rather than another change to this focused fix.
