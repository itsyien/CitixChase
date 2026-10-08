"""Compare real, separately operated game receipts; not a substitute for running two PCs."""
import argparse
import json
import re
from pathlib import Path


def compare(a, b):
    errors = []
    for label, report in (("Host", a), ("Guest", b)):
        if report.get("success") is not True or report.get("replicated_match") is not True:
            errors.append(f"{label}: no successful replicated match proof")
        if report.get("players") != 2:
            errors.append(f"{label}: expected two players")
        if report.get("connection_type") != "Relayed":
            errors.append(f"{label}: relay connection not observed")
        if not re.fullmatch(r"[0-9a-f]{32}", str(report.get("puid", ""))):
            errors.append(f"{label}: invalid guest identity")
        if report.get("eos_listener") is not True:
            errors.append(f"{label}: EOS game transport not observed")
    if a.get("action") != "Host" or b.get("action") != "Join":
        errors.append("Need a Host receipt and a Join receipt")
    if a.get("puid") == b.get("puid"):
        errors.append("Two distinct guest identities are required")
    if a.get("remote_puid") != b.get("puid") or b.get("remote_puid") != a.get("puid"):
        errors.append("Transport peer identities do not match")
    for key in ("session_id", "city_seed", "city_hash"):
        if a.get(key) is None or a.get(key) != b.get(key):
            errors.append(f"Shared {key} does not match")
    if not a.get("session_id"):
        errors.append("Missing session ID")
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host", type=Path)
    parser.add_argument("guest", type=Path)
    args = parser.parse_args()
    try:
        errors = compare(json.loads(args.host.read_text(encoding="utf-8-sig")),
                         json.loads(args.guest.read_text(encoding="utf-8-sig")))
    except (OSError, ValueError, TypeError) as error:
        print(f"FAIL: {error}")
        return 2
    if errors:
        print("FAIL: " + "; ".join(errors))
        return 2
    print("PASS: distinct guests observed each other over EOS relay in the same replicated pursuit")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
