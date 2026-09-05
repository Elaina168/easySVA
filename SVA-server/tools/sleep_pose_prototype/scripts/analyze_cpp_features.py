#!/usr/bin/env python3
"""Summarize per-frame diagnostics produced by PoseSmokeTest."""

from __future__ import annotations

import argparse
import csv
from collections import Counter
from pathlib import Path
from statistics import median


METRICS = (
    "head_height_ratio",
    "head_pitch_proxy_deg",
    "head_side_ratio",
    "head_arm_distance_ratio",
    "torso_angle_deg",
    "head_motion_ratio",
    "sleep_score",
    "valid_ratio",
    "positive_ratio",
)


def parse_float(value: str) -> float | None:
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("features", type=Path, help="PoseSmokeTest features.csv")
    args = parser.parse_args()

    with args.features.open("r", encoding="utf-8", newline="") as source:
        rows = list(csv.DictReader(source))

    valid_rows = [row for row in rows if row.get("features_valid") == "1"]
    candidates = [row for row in rows if row.get("candidate") == "1"]
    alerts = [row for row in rows if row.get("alert") == "1"]
    print(f"file={args.features}")
    print(
        f"rows={len(rows)} valid={len(valid_rows)} "
        f"candidates={len(candidates)} alerts={len(alerts)}"
    )
    print(f"states={dict(Counter(row.get('state', '') for row in rows))}")
    print(f"evidence={dict(Counter(row.get('evidence', '') for row in rows).most_common())}")

    for metric in METRICS:
        values = [value for row in rows if (value := parse_float(row.get(metric, ""))) is not None]
        if values:
            print(
                f"{metric}: min={min(values):.4f} "
                f"median={median(values):.4f} max={max(values):.4f}"
            )

    if alerts:
        print("alert_timestamps_ms=" + ",".join(row["timestamp_ms"] for row in alerts))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
