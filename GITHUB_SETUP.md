# GitHub source repository

This repository contains the CitixChase Unreal project, source, assets, tools and documentation. Generated builds, logs, caches and local EOS proof kits are excluded. Unreal assets and binary art files use Git LFS.

## Clone and configure

Install Git LFS, Python and the matching Unreal Engine version, then run these commands from the project root:

```powershell
git lfs install --local
git lfs pull
git config filter.citix-eos-config.clean "python Tools/git_sanitize_eos_config.py"
git config filter.citix-eos-config.smudge "python Tools/git_sanitize_eos_config.py --smudge"
git config filter.citix-eos-config.required true
```

Open `Citix.uproject` and regenerate/build the project as required for your machine. The engine module is named Citix.

## EOS local credentials

The committed `Config/DefaultGame.ini` has an empty `ClientSecret` value. Add the authorized client secret locally to enable EOS. The Git clean filter removes that value from commits while preserving the active configuration file. Configure this filter before editing or committing credentials on every clone; `.gitattributes` alone does not install the filter command.

Never commit the local EOS proof kit or packaged game configuration as a way to transfer credentials. Local launch/build scripts and `Saved` data remain on the development machine.

## Snapshot scope

The first commit captures the current development files. It includes work in progress; creating this repository does not claim that every change in the snapshot is a verified release. Existing v1.0 deployment and playtest evidence are documented separately under Docs.
