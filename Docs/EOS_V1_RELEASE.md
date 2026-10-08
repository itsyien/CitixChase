# CitixChase v1.0 deployment

Date: 2026-10-06, America/Denver.

- Unreal project version: 1.0. Lobby label: v1.0.
- Configuration: Win64 Shipping, UE 5.8.3.
- Package: E:/UnrealProjects/CitixChase/PackageRelease-v1.0.
- Deployment replaced: D:/_YienStudio/CitixChase.
- Previous deployment backup: E:/UnrealProjects/CitixChase/Saved/Backups/deployment-before-v1.0-20261006-213502.
- Build log: Saved/Logs/EOSOnline-Package-v1.0.log. BuildCookRun completed with exit 0 / BUILD SUCCESSFUL.
- Deployment verification: 34 files, exact expected count; every release file's SHA-256 matched the package manifest. Leftover debug symbols and debug manifest were removed. EOS shipping DLL is present.
- Startup smoke check: deployed bootstrap launched the deployed Shipping executable; its game window remained responding after 20 seconds. The release was left running.

Release refinement review: clear version identification 9/10, coherent player instructions 8/10, deployment integrity 9/10. The highest-impact corrections were replacing old LAN-only instructions, making the version visible in the lobby and removing obsolete deployment files. No gameplay redesign was needed.

Earlier two-device Development-build EOS relay and replicated pursuit evidence remains recorded in EOS_ONLINE_PLAYTEST.md. The Shipping build was freshly compiled, packaged and startup-tested; its two-device gameplay has not been repeated in this release pass. Use this same v1.0 package on both devices. Full combat/rematch and long-duration refresh remain broader playtest coverage items.
