"""Exercise actual portable entrypoint with deliberately blank credentials."""
import shutil
import subprocess
import tempfile
import unittest
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


class PortableLaunchTests(unittest.TestCase):
    def test_double_click_uses_executable_folder_and_keeps_failure_visible(self):
        with tempfile.TemporaryDirectory(prefix="Citix EOS 路径 ") as folder:
            target = Path(folder)
            for name in ("CitixEOSDeviceProof.exe", "EOSSDK-Win64-Shipping.dll"):
                shutil.copy2(ROOT / "EOSDeviceProofKit" / name, target / name)
            shutil.copy2(ROOT / "Tools/device-proof.example.ini", target / "device-proof.ini")
            result = subprocess.run([str(target / "CitixEOSDeviceProof.exe")], input="\n",
                                    capture_output=True, text=True, encoding="utf-8", cwd=ROOT, timeout=15)
            reports = list((target / "Reports").glob("*.json"))
            self.assertEqual(len(reports), 1, result.stdout + result.stderr)
            report = json.loads(reports[0].read_text())
            self.assertFalse(report["success"])
            self.assertEqual(report["error"], "Missing ClientSecret")
            self.assertIn("Press Enter to close", result.stdout)
            self.assertEqual(result.returncode, 2)

    def test_incomplete_extraction_has_persistent_actionable_error(self):
        with tempfile.TemporaryDirectory(prefix="Citix EOS missing ") as folder:
            target = Path(folder)
            for name in ("CitixEOSDeviceProof.exe", "EOSSDK-Win64-Shipping.dll"):
                shutil.copy2(ROOT / "EOSDeviceProofKit" / name, target / name)
            result = subprocess.run([str(target / "CitixEOSDeviceProof.exe")], input="\n",
                                    capture_output=True, text=True, encoding="utf-8", cwd=ROOT, timeout=15)
            self.assertIn("Extract all", result.stdout + result.stderr)
            self.assertIn("Press Enter to close", result.stdout)
            self.assertTrue((target / "startup-error.txt").exists())
            self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
