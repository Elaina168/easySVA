from __future__ import annotations

import unittest

from sleep_pose.tracker import SimpleIoUTracker, bbox_iou


class TrackerTest(unittest.TestCase):
    def test_iou(self) -> None:
        self.assertAlmostEqual(bbox_iou((0, 0, 10, 10), (0, 0, 10, 10)), 1.0)
        self.assertAlmostEqual(bbox_iou((0, 0, 10, 10), (10, 10, 20, 20)), 0.0)

    def test_id_is_stable_for_small_motion(self) -> None:
        tracker = SimpleIoUTracker(iou_threshold=0.3, max_missing_sec=1.0)
        first = tracker.update([(0, 0, 100, 100)], timestamp_ms=0)
        second = tracker.update([(5, 2, 105, 102)], timestamp_ms=100)
        self.assertEqual(first, second)

    def test_people_get_distinct_ids(self) -> None:
        tracker = SimpleIoUTracker(iou_threshold=0.3, max_missing_sec=1.0)
        ids = tracker.update([(0, 0, 50, 100), (200, 0, 250, 100)], timestamp_ms=0)
        self.assertEqual(len(set(ids)), 2)

    def test_expired_track_is_not_reused(self) -> None:
        tracker = SimpleIoUTracker(iou_threshold=0.3, max_missing_sec=1.0)
        old_id = tracker.update([(0, 0, 100, 100)], timestamp_ms=0)[0]
        tracker.update([], timestamp_ms=1500)
        new_id = tracker.update([(0, 0, 100, 100)], timestamp_ms=1600)[0]
        self.assertNotEqual(old_id, new_id)


if __name__ == "__main__":
    unittest.main()
