"""Bundle runtime files without debug symbols, local settings, or test/credential caches."""
from pathlib import Path
import zipfile

root = Path(__file__).resolve().parent.parent
package = root / "PackageOnline"
output = root / "CitixChase-Online-Test.zip"
files = [p for p in package.rglob("*") if p.is_file()
         and not {"Saved", "Reports"}.intersection(p.relative_to(package).parts)
         and p.suffix.lower() != ".pdb"
         and p.name != "Manifest_DebugFiles_Win64.txt"]
if not (package / "Windows/Citix.exe").is_file():
    raise SystemExit("Build PackageOnline first.")
with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
    for path in files:
        archive.write(path, "CitixChase-Online-Test/" + path.relative_to(package).as_posix())
with zipfile.ZipFile(output) as archive:
    if archive.testzip() is not None:
        raise SystemExit("ZIP integrity verification failed.")
print(f"Verified {output} ({len(files)} files, {output.stat().st_size / 1048576:.1f} MiB)")
