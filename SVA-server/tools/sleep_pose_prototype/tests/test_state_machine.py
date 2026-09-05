from __future__ import annotations

import unittest
from dataclasses import replace

from sleep_pose.config import PrototypeConfig
from sleep_pose.features import PoseFeatures
from sleep_pose.state_machine import SleepState, SleepStateMachine


def features(*, sleeping: bool, valid: bool = True) -> PoseFeatures:
    if not valid:
        return PoseFeatures(valid=False, invalid_reason="occluded")
    if sleeping:
        return PoseFeatures(
            valid=True,
            shoulder_width=100.0,
            shoulder_angle_deg=2.0,
            head_height_ratio=0.2,
            head_side_ratio=0.3,
            head_arm_distance_ratio=0.15,
            torso_angle_deg=5.0,
            head_x=180.0,
            head_y=180.0,
            shoulder_center_x=150.0,
            shoulder_center_y=200.0,
        )
    return PoseFeatures(
        valid=True,
        shoulder_width=100.0,
        shoulder_angle_deg=2.0,
        head_height_ratio=1.0,
        head_side_ratio=0.0,
        head_arm_distance_ratio=2.0,
        torso_angle_deg=2.0,
        head_x=150.0,
        head_y=100.0,
        shoulder_center_x=150.0,
        shoulder_center_y=200.0,
    )


def config() -> PrototypeConfig:
    return replace(
        PrototypeConfig(),
        confirm_window_sec=5.0,
        sleep_positive_ratio=0.8,
        minimum_valid_ratio=0.6,
        recovery_sec=2.0,
        motion_window_sec=1.0,
    )


class StateMachineTest(unittest.TestCase):
    def test_sustained_pose_raises_only_one_alert(self) -> None:
        machine = SleepStateMachine(config())
        decisions = [machine.update(7, second * 1000, features(sleeping=True)) for second in range(7)]
        self.assertEqual(sum(decision.alert for decision in decisions), 1)
        self.assertEqual(decisions[-1].state, SleepState.SLEEP)

    def test_short_head_down_does_not_alert(self) -> None:
        machine = SleepStateMachine(config())
        decisions = []
        for second in range(4):
            decisions.append(machine.update(1, second * 1000, features(sleeping=True)))
        for second in range(4, 8):
            decisions.append(machine.update(1, second * 1000, features(sleeping=False)))
        self.assertFalse(any(decision.alert for decision in decisions))
        self.assertEqual(decisions[-1].state, SleepState.NORMAL)

    def test_invalid_keypoints_never_count_as_sleep(self) -> None:
        machine = SleepStateMachine(config())
        decisions = [machine.update(1, second * 1000, features(sleeping=False, valid=False)) for second in range(20)]
        self.assertFalse(any(decision.alert for decision in decisions))
        self.assertEqual(decisions[-1].state, SleepState.NORMAL)

    def test_sleep_recovers_after_continuous_normal_pose(self) -> None:
        machine = SleepStateMachine(config())
        for second in range(7):
            decision = machine.update(1, second * 1000, features(sleeping=True))
        self.assertEqual(decision.state, SleepState.SLEEP)

        decision = machine.update(1, 7000, features(sleeping=False))
        self.assertEqual(decision.state, SleepState.RECOVER)
        decision = machine.update(1, 8000, features(sleeping=False))
        self.assertEqual(decision.state, SleepState.RECOVER)
        decision = machine.update(1, 9000, features(sleeping=False))
        self.assertEqual(decision.state, SleepState.NORMAL)


if __name__ == "__main__":
    unittest.main()
