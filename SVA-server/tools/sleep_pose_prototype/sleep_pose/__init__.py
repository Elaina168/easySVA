"""Pose-based sleep-at-post detector prototype for easySVA."""

from .config import PrototypeConfig, load_config
from .features import PoseFeatures, extract_pose_features
from .state_machine import SleepDecision, SleepState, SleepStateMachine
from .tracker import SimpleIoUTracker

__all__ = [
    "PoseFeatures",
    "PrototypeConfig",
    "SimpleIoUTracker",
    "SleepDecision",
    "SleepState",
    "SleepStateMachine",
    "extract_pose_features",
    "load_config",
]

