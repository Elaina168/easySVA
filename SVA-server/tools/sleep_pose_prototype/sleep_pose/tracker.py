from __future__ import annotations

from dataclasses import dataclass
from typing import Sequence


BBox = tuple[float, float, float, float]


def bbox_iou(first: BBox, second: BBox) -> float:
    left = max(first[0], second[0])
    top = max(first[1], second[1])
    right = min(first[2], second[2])
    bottom = min(first[3], second[3])
    intersection = max(0.0, right - left) * max(0.0, bottom - top)
    first_area = max(0.0, first[2] - first[0]) * max(0.0, first[3] - first[1])
    second_area = max(0.0, second[2] - second[0]) * max(0.0, second[3] - second[1])
    union = first_area + second_area - intersection
    return intersection / union if union > 0.0 else 0.0


@dataclass
class _Track:
    track_id: int
    bbox: BBox
    last_seen_ms: int


class SimpleIoUTracker:
    """Small deterministic tracker matching easySVA's current integration level."""

    def __init__(self, iou_threshold: float = 0.30, max_missing_sec: float = 1.0):
        self.iou_threshold = iou_threshold
        self.max_missing_ms = int(max_missing_sec * 1000.0)
        self._tracks: dict[int, _Track] = {}
        self._next_track_id = 1

    def update(self, boxes: Sequence[BBox], timestamp_ms: int) -> list[int]:
        self._remove_expired(timestamp_ms)
        assignments = [-1] * len(boxes)

        candidates: list[tuple[float, int, int]] = []
        for detection_index, box in enumerate(boxes):
            for track_id, track in self._tracks.items():
                score = bbox_iou(box, track.bbox)
                if score >= self.iou_threshold:
                    candidates.append((score, detection_index, track_id))
        candidates.sort(reverse=True)

        used_detections: set[int] = set()
        used_tracks: set[int] = set()
        for _, detection_index, track_id in candidates:
            if detection_index in used_detections or track_id in used_tracks:
                continue
            assignments[detection_index] = track_id
            used_detections.add(detection_index)
            used_tracks.add(track_id)

        for detection_index, box in enumerate(boxes):
            track_id = assignments[detection_index]
            if track_id < 0:
                track_id = self._next_track_id
                self._next_track_id += 1
                assignments[detection_index] = track_id
            self._tracks[track_id] = _Track(track_id, box, timestamp_ms)

        return assignments

    def _remove_expired(self, timestamp_ms: int) -> None:
        expired = [
            track_id
            for track_id, track in self._tracks.items()
            if timestamp_ms - track.last_seen_ms > self.max_missing_ms
        ]
        for track_id in expired:
            del self._tracks[track_id]

    def active_track_ids(self) -> set[int]:
        return set(self._tracks)

    def reset(self) -> None:
        self._tracks.clear()
        self._next_track_id = 1

