"""Compare the final 2,000 rendered frames of two matched UE CSV captures."""
import csv
import json
import statistics
import sys
from pathlib import Path

csv.field_size_limit(10_000_000)


def summarize(path):
    rows = []
    with Path(path).open(newline="") as source:
        for row in csv.DictReader(source):
            try:
                if float(row["FrameTime"]) > 0 and float(row["RHI/DrawCalls"]) > 0:
                    rows.append(row)
            except (ValueError, TypeError):
                continue  # UE's final metadata rows are not frame samples.
    assert len(rows) >= 2000, "Capture needs 2,000 steady rendered frames"
    rows = rows[-2000:]
    frames = sorted(float(row["FrameTime"]) for row in rows)
    return {
        "samples": len(rows),
        "mean_frame_ms": statistics.mean(frames),
        "median_frame_ms": statistics.median(frames),
        "p95_frame_ms": frames[int(len(frames) * .95) - 1],
        "mean_draw_calls": statistics.mean(float(row["RHI/DrawCalls"]) for row in rows),
        "mean_primitives": statistics.mean(float(row["RHI/PrimitivesDrawn"]) for row in rows),
    }


if __name__ == "__main__":
    baseline, current = map(summarize, sys.argv[1:3])
    regression = (current["mean_frame_ms"] / baseline["mean_frame_ms"] - 1) * 100
    print(json.dumps({"baseline": baseline, "current": current,
                      "mean_regression_percent": regression,
                      "within_ten_percent_target": regression <= 10}, indent=2))
