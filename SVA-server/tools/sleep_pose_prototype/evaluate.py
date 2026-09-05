#!/usr/bin/env python3
"""Evaluate frame-level sleep decisions against labelled time intervals."""

from __future__ import annotations

import argparse
import csv
import json
import math
from dataclasses import dataclass
from pathlib import Path


POSITIVE_LABELS = {"sleep"}
NEGATIVE_LABELS = {"normal", "short_head_down"}


@dataclass(frozen=True)
class LabelInterval:
    start_sec: float
    end_sec: float
    label: str
    track_id: int | None = None


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Evaluate prototype CSV output.")
    parser.add_argument("--features", type=Path, required=True, help="frame_features.csv")
    parser.add_argument("--labels", type=Path, required=True, help="Label interval CSV")
    parser.add_argument("--output", type=Path, default=Path("evaluation.json"))
    parser.add_argument(
        "--assume-unlabeled-normal",
        action="store_true",
        help="Treat frames outside intervals as normal instead of ignoring them.",
    )
    return parser.parse_args()


def load_labels(path: Path) -> list[LabelInterval]:
    intervals: list[LabelInterval] = []
    with path.open("r", encoding="utf-8-sig", newline="") as stream:
        for line, row in enumerate(csv.DictReader(stream), start=2):
            try:
                start = float(row["start_sec"])
                end = float(row["end_sec"])
                label = row["label"].strip().lower()
                raw_track_id = (row.get("track_id") or "").strip()
                track_id = int(raw_track_id) if raw_track_id else None
            except (KeyError, TypeError, ValueError) as exc:
                raise ValueError(f"Invalid label row at line {line}: {row}") from exc
            if not math.isfinite(start) or not math.isfinite(end) or start < 0 or end <= start:
                raise ValueError(f"Invalid time interval at line {line}: {start}..{end}")
            if label not in POSITIVE_LABELS | NEGATIVE_LABELS:
                raise ValueError(f"Unsupported label at line {line}: {label}")
            intervals.append(LabelInterval(start, end, label, track_id))
    intervals.sort(key=lambda item: (item.track_id is not None, item.track_id or -1, item.start_sec))
    for index, previous in enumerate(intervals):
        for current in intervals[index + 1 :]:
            if current.track_id != previous.track_id:
                continue
            if current.start_sec < previous.end_sec and previous.start_sec < current.end_sec:
                raise ValueError(f"Overlapping intervals: {previous} and {current}")
    return intervals


def safe_ratio(numerator: int, denominator: int) -> float:
    return numerator / denominator if denominator else 0.0


def evaluate(
    features_path: Path,
    intervals: list[LabelInterval],
    assume_unlabeled_normal: bool,
) -> dict[str, object]:
    tp = fp = fn = tn = ignored = 0
    first_sleep_detection: dict[int, float] = {}

    with features_path.open("r", encoding="utf-8-sig", newline="") as stream:
        for line, row in enumerate(csv.DictReader(stream), start=2):
            try:
                timestamp = float(row["timestamp_sec"])
                track_id = int(row["track_id"])
                prediction = row["state"].strip().upper() == "SLEEP"
            except (KeyError, TypeError, ValueError) as exc:
                raise ValueError(f"Invalid feature row at line {line}: {row}") from exc

            matched_index = None
            actual_label = None
            matching_intervals = [
                (index, interval)
                for index, interval in enumerate(intervals)
                if interval.track_id == track_id
                and interval.start_sec <= timestamp < interval.end_sec
            ]
            if not matching_intervals:
                matching_intervals = [
                    (index, interval)
                    for index, interval in enumerate(intervals)
                    if interval.track_id is None
                    and interval.start_sec <= timestamp < interval.end_sec
                ]
            for index, interval in matching_intervals[:1]:
                    matched_index = index
                    actual_label = interval.label

            if actual_label is None:
                if not assume_unlabeled_normal:
                    ignored += 1
                    continue
                actual_label = "normal"

            actual = actual_label in POSITIVE_LABELS
            if actual and prediction:
                tp += 1
                if matched_index is not None:
                    first_sleep_detection.setdefault(matched_index, timestamp)
            elif actual:
                fn += 1
            elif prediction:
                fp += 1
            else:
                tn += 1

    precision = safe_ratio(tp, tp + fp)
    recall = safe_ratio(tp, tp + fn)
    f1 = 2 * precision * recall / (precision + recall) if precision + recall else 0.0

    event_delays: list[dict[str, object]] = []
    delay_values: list[float] = []
    for index, interval in enumerate(intervals):
        if interval.label not in POSITIVE_LABELS:
            continue
        detected_at = first_sleep_detection.get(index)
        delay = None if detected_at is None else max(0.0, detected_at - interval.start_sec)
        if delay is not None:
            delay_values.append(delay)
        event_delays.append(
            {
                "start_sec": interval.start_sec,
                "end_sec": interval.end_sec,
                "track_id": interval.track_id,
                "detected": detected_at is not None,
                "first_detection_sec": detected_at,
                "delay_sec": delay,
            }
        )

    return {
        "frame_metrics": {
            "tp": tp,
            "fp": fp,
            "fn": fn,
            "tn": tn,
            "evaluated_frames": tp + fp + fn + tn,
            "ignored_frames": ignored,
            "precision": precision,
            "recall": recall,
            "f1": f1,
        },
        "event_metrics": {
            "sleep_events": len(event_delays),
            "detected_events": len(delay_values),
            "missed_events": len(event_delays) - len(delay_values),
            "mean_detection_delay_sec": (
                sum(delay_values) / len(delay_values) if delay_values else None
            ),
            "max_detection_delay_sec": max(delay_values) if delay_values else None,
            "events": event_delays,
        },
    }


def main() -> int:
    args = parse_args()
    try:
        intervals = load_labels(args.labels)
        report = evaluate(args.features, intervals, args.assume_unlabeled_normal)
    except (OSError, ValueError) as exc:
        raise SystemExit(str(exc)) from exc

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(json.dumps(report, ensure_ascii=False, indent=2))
    print(f"Saved: {args.output.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
