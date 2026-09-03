from __future__ import annotations

from dataclasses import asdict, dataclass, fields
from pathlib import Path
from typing import Any, Mapping


@dataclass(frozen=True)
class PrototypeConfig:
    model: str = "yolo11n-pose.pt"
    image_size: int = 640
    detection_confidence: float = 0.35
    nms_iou: float = 0.45
    keypoint_confidence: float = 0.35

    tracker_iou_threshold: float = 0.30
    tracker_max_missing_sec: float = 1.0

    confirm_window_sec: float = 15.0
    sleep_positive_ratio: float = 0.80
    minimum_valid_ratio: float = 0.60
    recovery_sec: float = 2.0

    head_height_ratio_max: float = 0.45
    head_side_ratio_min: float = 0.30
    head_arm_distance_ratio_max: float = 0.75
    torso_angle_deg_min: float = 25.0
    shoulder_tilt_deg_min: float = 15.0
    motion_window_sec: float = 2.0
    head_motion_ratio_max: float = 0.15

    def __post_init__(self) -> None:
        if self.image_size <= 0:
            raise ValueError("image_size must be positive")
        for name in (
            "detection_confidence",
            "nms_iou",
            "keypoint_confidence",
            "tracker_iou_threshold",
            "sleep_positive_ratio",
            "minimum_valid_ratio",
        ):
            value = float(getattr(self, name))
            if not 0.0 <= value <= 1.0:
                raise ValueError(f"{name} must be between 0 and 1")
        for name in (
            "tracker_max_missing_sec",
            "confirm_window_sec",
            "recovery_sec",
            "motion_window_sec",
            "head_motion_ratio_max",
        ):
            if float(getattr(self, name)) <= 0.0:
                raise ValueError(f"{name} must be positive")

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)

    @classmethod
    def from_mapping(cls, values: Mapping[str, Any]) -> "PrototypeConfig":
        allowed = {item.name for item in fields(cls)}
        unknown = sorted(set(values) - allowed)
        if unknown:
            raise ValueError(f"unknown configuration keys: {', '.join(unknown)}")
        return cls(**dict(values))


def load_config(path: str | Path | None = None) -> PrototypeConfig:
    if path is None:
        return PrototypeConfig()

    try:
        import yaml
    except ImportError as exc:  # pragma: no cover - dependency error path
        raise RuntimeError("PyYAML is required to load a YAML configuration") from exc

    config_path = Path(path)
    with config_path.open("r", encoding="utf-8") as stream:
        values = yaml.safe_load(stream) or {}
    if not isinstance(values, dict):
        raise ValueError("configuration root must be a YAML mapping")
    return PrototypeConfig.from_mapping(values)

