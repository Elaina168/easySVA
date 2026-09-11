from __future__ import annotations

from collections import deque
from dataclasses import dataclass, field
from enum import Enum
from math import hypot

from .config import PrototypeConfig
from .features import PoseFeatures


class SleepState(str, Enum):
    NORMAL = "NORMAL"
    SUSPECT = "SUSPECT"
    SLEEP = "SLEEP"
    RECOVER = "RECOVER"


@dataclass(frozen=True)
class SleepDecision:
    state: SleepState
    previous_state: SleepState
    transitioned: bool
    alert: bool
    candidate: bool
    sleep_score: float
    valid_ratio: float
    positive_ratio: float
    head_motion_ratio: float | None
    reason: str


@dataclass
class _Observation:
    timestamp_ms: int
    valid: bool
    candidate: bool
    score: float
    head_x: float | None
    head_y: float | None
    shoulder_width: float | None


@dataclass
class _TrackContext:
    state: SleepState = SleepState.NORMAL
    state_since_ms: int = 0
    last_seen_ms: int = 0
    negative_since_ms: int | None = None
    observations: deque[_Observation] = field(default_factory=deque)


class SleepStateMachine:
    """Time-based, per-track sleep-posture state machine.

    The model only supplies pose keypoints. This class deliberately keeps the
    behavior decision outside the model so thresholds remain explainable and
    can be tuned per camera without retraining.
    """

    def __init__(self, config: PrototypeConfig):
        self.config = config
        self._contexts: dict[int, _TrackContext] = {}

    def update(
        self,
        track_id: int,
        timestamp_ms: int,
        features: PoseFeatures,
    ) -> SleepDecision:
        context = self._contexts.setdefault(
            track_id,
            _TrackContext(state_since_ms=timestamp_ms, last_seen_ms=timestamp_ms),
        )
        context.last_seen_ms = timestamp_ms
        previous_state = context.state

        candidate, score, motion_ratio, evidence_reason = self._assess_candidate(
            context, timestamp_ms, features
        )
        context.observations.append(
            _Observation(
                timestamp_ms=timestamp_ms,
                valid=features.valid,
                candidate=candidate,
                score=score,
                head_x=features.head_x,
                head_y=features.head_y,
                shoulder_width=features.shoulder_width,
            )
        )
        self._trim_history(context, timestamp_ms)
        valid_ratio, positive_ratio = self._window_ratios(context, timestamp_ms)

        alert = False
        if features.valid:
            if context.state == SleepState.NORMAL:
                if candidate:
                    self._transition(context, SleepState.SUSPECT, timestamp_ms)

            elif context.state == SleepState.SUSPECT:
                if candidate:
                    context.negative_since_ms = None
                    elapsed_ms = timestamp_ms - context.state_since_ms
                    if (
                        elapsed_ms >= self._milliseconds(self.config.confirm_window_sec)
                        and valid_ratio >= self.config.minimum_valid_ratio
                        and positive_ratio >= self.config.sleep_positive_ratio
                    ):
                        self._transition(context, SleepState.SLEEP, timestamp_ms)
                        alert = True
                else:
                    self._handle_negative(context, timestamp_ms, SleepState.NORMAL)

            elif context.state == SleepState.SLEEP:
                if candidate:
                    context.negative_since_ms = None
                else:
                    self._transition(context, SleepState.RECOVER, timestamp_ms)
                    context.negative_since_ms = timestamp_ms

            elif context.state == SleepState.RECOVER:
                if candidate:
                    # A brief upright/occluded interval is still the same event.
                    self._transition(context, SleepState.SLEEP, timestamp_ms)
                else:
                    self._handle_negative(context, timestamp_ms, SleepState.NORMAL)

        return SleepDecision(
            state=context.state,
            previous_state=previous_state,
            transitioned=context.state != previous_state,
            alert=alert,
            candidate=candidate,
            sleep_score=score,
            valid_ratio=valid_ratio,
            positive_ratio=positive_ratio,
            head_motion_ratio=motion_ratio,
            reason=evidence_reason if features.valid else features.invalid_reason,
        )

    def expire_missing(self, timestamp_ms: int, visible_track_ids: set[int]) -> None:
        tolerance_ms = self._milliseconds(self.config.tracker_max_missing_sec)
        expired = [
            track_id
            for track_id, context in self._contexts.items()
            if track_id not in visible_track_ids
            and timestamp_ms - context.last_seen_ms > tolerance_ms
        ]
        for track_id in expired:
            del self._contexts[track_id]

    def state_for(self, track_id: int) -> SleepState | None:
        context = self._contexts.get(track_id)
        return context.state if context else None

    def reset(self) -> None:
        self._contexts.clear()

    def _assess_candidate(
        self,
        context: _TrackContext,
        timestamp_ms: int,
        features: PoseFeatures,
    ) -> tuple[bool, float, float | None, str]:
        if not features.valid or features.head_height_ratio is None:
            return False, 0.0, None, features.invalid_reason

        motion_ratio = self._head_motion_ratio(context, timestamp_ms, features)
        head_low = features.head_height_ratio <= self.config.head_height_ratio_max
        side_lean = (
            features.head_side_ratio is not None
            and features.head_side_ratio >= self.config.head_side_ratio_min
        )
        arm_support = (
            features.head_arm_distance_ratio is not None
            and features.head_arm_distance_ratio
            <= self.config.head_arm_distance_ratio_max
        )
        torso_forward = (
            features.torso_angle_deg is not None
            and features.torso_angle_deg >= self.config.torso_angle_deg_min
        )
        shoulder_tilt = (
            features.shoulder_angle_deg is not None
            and abs(features.shoulder_angle_deg)
            >= self.config.shoulder_tilt_deg_min
        )
        low_motion = (
            motion_ratio is not None
            and motion_ratio <= self.config.head_motion_ratio_max
        )

        supports = {
            "side_lean": side_lean,
            "head_near_arm": arm_support,
            "torso_forward": torso_forward,
            "shoulder_tilt": shoulder_tilt,
            "low_motion": low_motion,
        }
        active_supports = [name for name, active in supports.items() if active]
        candidate = head_low and bool(active_supports)

        # Score is diagnostic only; the state transition uses explicit evidence
        # and temporal ratios. This keeps decisions explainable during tuning.
        margin = 0.35
        head_strength = max(
            0.0,
            min(
                1.0,
                (self.config.head_height_ratio_max + margin - features.head_height_ratio)
                / (2.0 * margin),
            ),
        )
        support_strength = len(active_supports) / len(supports)
        score = 0.60 * head_strength + 0.40 * support_strength
        reason = "head_not_low"
        if head_low:
            reason = "head_low"
            if active_supports:
                reason += "+" + "+".join(active_supports)
            else:
                reason += "+no_supporting_evidence"
        return candidate, score, motion_ratio, reason

    def _head_motion_ratio(
        self,
        context: _TrackContext,
        timestamp_ms: int,
        features: PoseFeatures,
    ) -> float | None:
        if (
            features.head_x is None
            or features.head_y is None
            or features.shoulder_width is None
            or features.shoulder_width < 1.0
        ):
            return None

        start_ms = timestamp_ms - self._milliseconds(self.config.motion_window_sec)
        references = [
            observation
            for observation in context.observations
            if observation.valid
            and observation.timestamp_ms >= start_ms
            and observation.head_x is not None
            and observation.head_y is not None
        ]
        if not references:
            return None
        reference = references[0]
        if timestamp_ms - reference.timestamp_ms < 500:
            return None
        displacement = hypot(
            features.head_x - float(reference.head_x),
            features.head_y - float(reference.head_y),
        )
        return displacement / features.shoulder_width

    def _window_ratios(
        self, context: _TrackContext, timestamp_ms: int
    ) -> tuple[float, float]:
        start_ms = timestamp_ms - self._milliseconds(self.config.confirm_window_sec)
        window = [item for item in context.observations if item.timestamp_ms >= start_ms]
        if not window:
            return 0.0, 0.0
        valid = [item for item in window if item.valid]
        valid_ratio = len(valid) / len(window)
        positive_ratio = (
            sum(1 for item in valid if item.candidate) / len(valid) if valid else 0.0
        )
        return valid_ratio, positive_ratio

    def _trim_history(self, context: _TrackContext, timestamp_ms: int) -> None:
        keep_sec = max(
            self.config.confirm_window_sec * 2.0,
            self.config.motion_window_sec * 2.0,
        )
        oldest_ms = timestamp_ms - self._milliseconds(keep_sec)
        while context.observations and context.observations[0].timestamp_ms < oldest_ms:
            context.observations.popleft()

    def _handle_negative(
        self,
        context: _TrackContext,
        timestamp_ms: int,
        target_state: SleepState,
    ) -> None:
        if context.negative_since_ms is None:
            context.negative_since_ms = timestamp_ms
            return
        if timestamp_ms - context.negative_since_ms >= self._milliseconds(
            self.config.recovery_sec
        ):
            self._transition(context, target_state, timestamp_ms)

    @staticmethod
    def _transition(
        context: _TrackContext, state: SleepState, timestamp_ms: int
    ) -> None:
        context.state = state
        context.state_since_ms = timestamp_ms
        context.negative_since_ms = None

    @staticmethod
    def _milliseconds(seconds: float) -> int:
        return int(seconds * 1000.0)
