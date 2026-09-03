from __future__ import annotations

import unittest

from sleep_pose.features import COCO_KEYPOINT_NAMES, extract_pose_features


INDEX = {name: index for index, name in enumerate(COCO_KEYPOINT_NAMES)}


def pose() -> list[list[float]]:
    return [[0.0, 0.0, 0.0] for _ in range(17)]


def set_point(points: list[list[float]], name: str, x: float, y: float) -> None:
    points[INDEX[name]] = [x, y, 0.95]


class FeatureExtractionTest(unittest.TestCase):
    def test_upright_pose_has_head_above_shoulders(self) -> None:
        points = pose()
        for name, x, y in (
            ("nose", 150, 95),
            ("left_eye", 140, 90),
            ("right_eye", 160, 90),
            ("left_ear", 130, 100),
            ("right_ear", 170, 100),
            ("left_shoulder", 100, 200),
            ("right_shoulder", 200, 200),
            ("left_hip", 115, 320),
            ("right_hip", 185, 320),
        ):
            set_point(points, name, x, y)

        result = extract_pose_features(points, minimum_confidence=0.35)

        self.assertTrue(result.valid)
        self.assertAlmostEqual(result.shoulder_width, 100.0)
        self.assertGreater(result.head_height_ratio, 0.9)
        self.assertAlmostEqual(result.head_side_ratio, 0.0)
        self.assertLess(result.torso_angle_deg, 1.0)

    def test_head_down_near_arm_is_detected_geometrically(self) -> None:
        points = pose()
        for name, x, y in (
            ("nose", 180, 180),
            ("left_eye", 174, 175),
            ("right_eye", 186, 175),
            ("left_shoulder", 100, 200),
            ("right_shoulder", 200, 200),
            ("left_wrist", 180, 190),
            ("left_hip", 115, 320),
            ("right_hip", 185, 320),
        ):
            set_point(points, name, x, y)

        result = extract_pose_features(points, minimum_confidence=0.35)

        self.assertTrue(result.valid)
        self.assertLess(result.head_height_ratio, 0.45)
        self.assertLess(result.head_arm_distance_ratio, 0.2)

    def test_missing_shoulder_makes_pose_unknown(self) -> None:
        points = pose()
        set_point(points, "nose", 150, 100)
        set_point(points, "left_shoulder", 100, 200)
        result = extract_pose_features(points, minimum_confidence=0.35)
        self.assertFalse(result.valid)
        self.assertIn("shoulders", result.invalid_reason)

    def test_non_coco_keypoint_count_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            extract_pose_features([[0.0, 0.0, 0.0]], minimum_confidence=0.35)


if __name__ == "__main__":
    unittest.main()
