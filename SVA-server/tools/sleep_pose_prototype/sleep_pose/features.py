from __future__ import annotations

from dataclasses import asdict, dataclass
from math import atan2, degrees, hypot, isfinite
from typing import Any, Iterable, Sequence


COCO_KEYPOINT_NAMES = (
    "nose",
    "left_eye",
    "right_eye",
    "left_ear",
    "right_ear",
    "left_shoulder",
    "right_shoulder",
    "left_elbow",
    "right_elbow",
    "left_wrist",
    "right_wrist",
    "left_hip",
    "right_hip",
    "left_knee",
    "right_knee",
    "left_ankle",
    "right_ankle",
)

HEAD_INDICES = (0, 1, 2, 3, 4)
SHOULDER_INDICES = (5, 6)
ARM_INDICES = (7, 8, 9, 10)
HIP_INDICES = (11, 12)


@dataclass(frozen=True)
class Keypoint:
    x: float
    y: float
    confidence: float

    def is_valid(self, minimum_confidence: float) -> bool:
        return (
            isfinite(self.x)
            and isfinite(self.y)
            and isfinite(self.confidence)
            and self.confidence >= minimum_confidence
        )


@dataclass(frozen=True)
class PoseFeatures:
    valid: bool
    invalid_reason: str = ""
    valid_keypoint_count: int = 0
    head_x: float | None = None
    head_y: float | None = None
    shoulder_center_x: float | None = None
    shoulder_center_y: float | None = None
    shoulder_width: float | None = None
    head_height_ratio: float | None = None
    head_pitch_proxy_deg: float | None = None
    head_side_ratio: float | None = None
    shoulder_angle_deg: float | None = None
    head_arm_distance_ratio: float | None = None
    torso_angle_deg: float | None = None

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


def _parse_keypoints(values: Sequence[Sequence[float]]) -> list[Keypoint]:
    if len(values) != len(COCO_KEYPOINT_NAMES):
        raise ValueError(
            f"expected {len(COCO_KEYPOINT_NAMES)} COCO keypoints, got {len(values)}"
        )
    parsed: list[Keypoint] = []
    for index, raw in enumerate(values):
        if len(raw) < 3:
            raise ValueError(f"keypoint {index} must contain x, y and confidence")
        parsed.append(Keypoint(float(raw[0]), float(raw[1]), float(raw[2])))
    return parsed


def _weighted_center(points: Iterable[Keypoint]) -> tuple[float, float]:
    selected = list(points)
    weight_sum = sum(point.confidence for point in selected)
    if weight_sum <= 0.0:
        raise ValueError("cannot calculate a center with zero confidence")
    x = sum(point.x * point.confidence for point in selected) / weight_sum
    y = sum(point.y * point.confidence for point in selected) / weight_sum
    return x, y


def extract_pose_features(
    raw_keypoints: Sequence[Sequence[float]],
    minimum_confidence: float = 0.35,
) -> PoseFeatures:
    """Extract scale-independent head/shoulder features from COCO keypoints.

    Both shoulders and at least one head landmark are mandatory. Hips and arms
    are optional so a desk or control panel may occlude the lower body without
    making the whole observation unusable.
    """

    keypoints = _parse_keypoints(raw_keypoints)
    valid = [point.is_valid(minimum_confidence) for point in keypoints]
    valid_count = sum(valid)

    if not all(valid[index] for index in SHOULDER_INDICES):
        return PoseFeatures(False, "both shoulders are required", valid_count)

    head_points = [keypoints[index] for index in HEAD_INDICES if valid[index]]
    if not head_points:
        return PoseFeatures(False, "at least one head keypoint is required", valid_count)

    left_shoulder = keypoints[SHOULDER_INDICES[0]]
    right_shoulder = keypoints[SHOULDER_INDICES[1]]
    shoulder_width = hypot(
        right_shoulder.x - left_shoulder.x,
        right_shoulder.y - left_shoulder.y,
    )
    if shoulder_width < 1.0:
        return PoseFeatures(False, "shoulder width is too small", valid_count)

    shoulder_center_x = (left_shoulder.x + right_shoulder.x) / 2.0
    shoulder_center_y = (left_shoulder.y + right_shoulder.y) / 2.0
    head_x, head_y = _weighted_center(head_points)

    head_height_ratio = (shoulder_center_y - head_y) / shoulder_width
    head_pitch_proxy_deg = degrees(atan2(shoulder_center_y - head_y, shoulder_width))
    head_side_ratio = abs(head_x - shoulder_center_x) / shoulder_width
    shoulder_angle_deg = degrees(
        atan2(
            right_shoulder.y - left_shoulder.y,
            right_shoulder.x - left_shoulder.x,
        )
    )
    if shoulder_angle_deg > 90.0:
        shoulder_angle_deg -= 180.0
    elif shoulder_angle_deg < -90.0:
        shoulder_angle_deg += 180.0

    arm_points = [keypoints[index] for index in ARM_INDICES if valid[index]]
    head_arm_distance_ratio: float | None = None
    if arm_points:
        head_arm_distance_ratio = min(
            hypot(head_x - point.x, head_y - point.y) for point in arm_points
        ) / shoulder_width

    torso_angle_deg: float | None = None
    if all(valid[index] for index in HIP_INDICES):
        left_hip = keypoints[HIP_INDICES[0]]
        right_hip = keypoints[HIP_INDICES[1]]
        hip_center_x = (left_hip.x + right_hip.x) / 2.0
        hip_center_y = (left_hip.y + right_hip.y) / 2.0
        torso_dx = hip_center_x - shoulder_center_x
        torso_dy = hip_center_y - shoulder_center_y
        if hypot(torso_dx, torso_dy) >= 1.0:
            torso_angle_deg = degrees(atan2(abs(torso_dx), abs(torso_dy)))

    return PoseFeatures(
        valid=True,
        valid_keypoint_count=valid_count,
        head_x=head_x,
        head_y=head_y,
        shoulder_center_x=shoulder_center_x,
        shoulder_center_y=shoulder_center_y,
        shoulder_width=shoulder_width,
        head_height_ratio=head_height_ratio,
        head_pitch_proxy_deg=head_pitch_proxy_deg,
        head_side_ratio=head_side_ratio,
        shoulder_angle_deg=shoulder_angle_deg,
        head_arm_distance_ratio=head_arm_distance_ratio,
        torso_angle_deg=torso_angle_deg,
    )
