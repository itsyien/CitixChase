"""Compare observed Device ID login reports; device tags are not hardware attestation."""
import argparse
import json
import re
from pathlib import Path


def compare_reports(first, second, restart=None):
    errors = []
    reports = [first, second] + ([restart] if restart is not None else [])
    for index, item in enumerate(reports, 1):
        if (item.get("schema") != 1 or item.get("success") is not True or
                item.get("auth_method") != "DeviceId" or item.get("login_status") != "LoggedIn" or
                not re.fullmatch(r"[0-9a-fA-F]{32}", str(item.get("puid", ""))) or
                not str(item.get("engine_version", "")).startswith("5.8.") or
                not item.get("device_tag") or item.get("runtime_kind") not in
                ("UnrealCommandlet", "UEBundledSDKPortable")):
            errors.append(f"Report {index} is not a successful UE 5.8 Device ID result.")
    for key in ("product_id", "sandbox_id", "deployment_id", "sdk_version"):
        if not first.get(key) or any(item.get(key) != first[key] for item in reports[1:]):
            errors.append(f"Reports do not share the same {key}.")
    if first.get("device_tag") == second.get("device_tag"):
        errors.append("Two reports from the same device/profile are not a two-device proof.")
    if first.get("puid", "").lower() == second.get("puid", "").lower():
        errors.append("Both devices returned the same PUID.")
    if restart is not None and (restart.get("device_tag") != first.get("device_tag") or
                               restart.get("puid", "").lower() != first.get("puid", "").lower()):
        errors.append("Device A did not retain its PUID across restart.")
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("device_a", type=Path)
    parser.add_argument("device_b", type=Path)
    parser.add_argument("--restart-a", type=Path, required=True)
    args = parser.parse_args()
    try:
        values = [json.loads(path.read_text(encoding="utf-8-sig"))
                  for path in (args.device_a, args.device_b, args.restart_a)]
        errors = compare_reports(*values)
    except (OSError, ValueError, TypeError, AttributeError) as error:
        print(f"FAIL: Cannot read valid reports: {type(error).__name__}")
        return 2
    for error in errors:
        print(f"FAIL: {error}")
    if errors:
        return 1
    print("PASS: distinct device/profile tags, distinct PUIDs, matching environment/SDK, stable device A restart.")
    print("Requires honest physical-device runs. This does not prove Unreal session registration or online rooms.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
