from __future__ import annotations

import csv
import tempfile
import unittest
from pathlib import Path

from evaluate import evaluate, load_labels


class EvaluationTest(unittest.TestCase):
    def test_metrics_and_event_delay(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            labels = root / "labels.csv"
            features = root / "features.csv"
            labels.write_text(
                "start_sec,end_sec,label,track_id\n"
                "0,2,normal,1\n"
                "2,5,sleep,1\n",
                encoding="utf-8",
            )
            with features.open("w", encoding="utf-8", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=["timestamp_sec", "track_id", "state"])
                writer.writeheader()
                writer.writerows(
                    [
                        {"timestamp_sec": 0, "track_id": 1, "state": "NORMAL"},
                        {"timestamp_sec": 1, "track_id": 1, "state": "SLEEP"},
                        {"timestamp_sec": 2, "track_id": 1, "state": "SUSPECT"},
                        {"timestamp_sec": 3, "track_id": 1, "state": "SLEEP"},
                        {"timestamp_sec": 4, "track_id": 1, "state": "SLEEP"},
                    ]
                )

            report = evaluate(features, load_labels(labels), False)
            frame = report["frame_metrics"]
            event = report["event_metrics"]
            self.assertEqual((frame["tp"], frame["fp"], frame["fn"], frame["tn"]), (2, 1, 1, 1))
            self.assertAlmostEqual(event["mean_detection_delay_sec"], 1.0)
            self.assertEqual(event["missed_events"], 0)

    def test_track_specific_labels_ignore_another_person(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            labels = root / "labels.csv"
            features = root / "features.csv"
            labels.write_text(
                "start_sec,end_sec,label,track_id\n0,5,sleep,1\n",
                encoding="utf-8",
            )
            features.write_text(
                "timestamp_sec,track_id,state\n"
                "1,1,SLEEP\n"
                "1,2,NORMAL\n",
                encoding="utf-8",
            )

            report = evaluate(features, load_labels(labels), False)
            self.assertEqual(report["frame_metrics"]["tp"], 1)
            self.assertEqual(report["frame_metrics"]["ignored_frames"], 1)


if __name__ == "__main__":
    unittest.main()
