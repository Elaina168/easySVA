from __future__ import annotations

import argparse
import csv
import json
import time
from pathlib import Path
from typing import Any

from sleep_pose import (
    SimpleIoUTracker,
    SleepState,
    SleepStateMachine,
    extract_pose_features,
    load_config,
)


FEATURE_COLUMNS = [
    "frame_id",
    "timestamp_ms",
    "timestamp_sec",
    "track_id",
    "bbox_x1",
    "bbox_y1",
    "bbox_x2",
    "bbox_y2",
    "pose_valid",
    "invalid_reason",
    "valid_keypoint_count",
    "head_x",
    "head_y",
    "shoulder_center_x",
    "shoulder_center_y",
    "shoulder_width",
    "head_height_ratio",
    "head_pitch_proxy_deg",
    "head_side_ratio",
    "shoulder_angle_deg",
    "head_arm_distance_ratio",
    "torso_angle_deg",
    "head_motion_ratio",
    "candidate",
    "sleep_score",
    "valid_ratio",
    "positive_ratio",
    "state",
    "transitioned",
    "evidence",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run the easySVA sleep-posture prototype on a local video or camera"
    )
    parser.add_argument("--source", required=True, help="video path, RTSP URL, or camera index")
    parser.add_argument("--config", default="config/default.yaml", help="YAML configuration")
    parser.add_argument("--model", help="override model from configuration")
    parser.add_argument("--output-dir", default="outputs/latest", help="result directory")
    parser.add_argument("--control-code", default="prototype", help="event control code")
    parser.add_argument("--no-video", action="store_true", help="do not write annotated MP4")
    return parser.parse_args()


def _optional(value: float | None) -> float | str:
    return "" if value is None else round(float(value), 6)


def _frame_timestamp_ms(
    capture: Any,
    frame_index: int,
    fps: float,
    is_file: bool,
    started_at: float,
) -> int:
    if is_file:
        reported = float(capture.get(0))  # cv2.CAP_PROP_POS_MSEC
        if reported > 0.0:
            return int(reported)
        return int(frame_index * 1000.0 / max(fps, 1.0))
    return int((time.monotonic() - started_at) * 1000.0)


def main() -> int:
    args = parse_args()

    try:
        import cv2
        from ultralytics import YOLO
    except ImportError as exc:
        raise SystemExit(
            "Missing runtime dependencies. Run: pip install -r requirements.txt"
        ) from exc

    config = load_config(args.config)
    model_name = args.model or config.model
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    source: str | int = int(args.source) if args.source.isdigit() else args.source
    is_file = isinstance(source, str) and Path(source).is_file()
    capture = cv2.VideoCapture(source)
    if not capture.isOpened():
        raise SystemExit(f"cannot open video source: {args.source}")

    fps = float(capture.get(cv2.CAP_PROP_FPS))
    if fps <= 0.0 or fps > 240.0:
        fps = 25.0

    model = YOLO(model_name)
    tracker = SimpleIoUTracker(
        config.tracker_iou_threshold, config.tracker_max_missing_sec
    )
    state_machine = SleepStateMachine(config)

    feature_path = output_dir / "frame_features.csv"
    event_path = output_dir / "events.jsonl"
    summary_path = output_dir / "summary.json"
    video_path = output_dir / "annotated.mp4"
    video_writer = None
    frame_count = 0
    detection_count = 0
    alert_count = 0
    started_at = time.monotonic()

    state_colors = {
        SleepState.NORMAL: (0, 200, 0),
        SleepState.SUSPECT: (0, 215, 255),
        SleepState.SLEEP: (0, 0, 255),
        SleepState.RECOVER: (255, 180, 0),
    }

    try:
        with feature_path.open("w", newline="", encoding="utf-8") as feature_stream, event_path.open(
            "w", encoding="utf-8"
        ) as event_stream:
            feature_writer = csv.DictWriter(feature_stream, fieldnames=FEATURE_COLUMNS)
            feature_writer.writeheader()

            while True:
                ok, frame = capture.read()
                if not ok:
                    break
                timestamp_ms = _frame_timestamp_ms(
                    capture, frame_count, fps, is_file, started_at
                )
                predictions = model.predict(
                    source=frame,
                    imgsz=config.image_size,
                    conf=config.detection_confidence,
                    iou=config.nms_iou,
                    verbose=False,
                )
                result = predictions[0]
                annotated = result.plot()

                boxes_array = (
                    result.boxes.xyxy.cpu().numpy()
                    if result.boxes is not None
                    else []
                )
                keypoints_array = (
                    result.keypoints.data.cpu().numpy()
                    if result.keypoints is not None
                    else []
                )
                pair_count = min(len(boxes_array), len(keypoints_array))
                boxes = [tuple(map(float, boxes_array[index][:4])) for index in range(pair_count)]
                track_ids = tracker.update(boxes, timestamp_ms)
                state_machine.expire_missing(timestamp_ms, set(track_ids))
                alerts_this_frame: list[dict[str, Any]] = []

                for index in range(pair_count):
                    bbox = boxes[index]
                    track_id = track_ids[index]
                    raw_keypoints = keypoints_array[index].tolist()
                    features = extract_pose_features(
                        raw_keypoints, config.keypoint_confidence
                    )
                    decision = state_machine.update(track_id, timestamp_ms, features)
                    detection_count += 1

                    row = {
                        "frame_id": frame_count,
                        "timestamp_ms": timestamp_ms,
                        "timestamp_sec": round(timestamp_ms / 1000.0, 3),
                        "track_id": track_id,
                        "bbox_x1": round(bbox[0], 3),
                        "bbox_y1": round(bbox[1], 3),
                        "bbox_x2": round(bbox[2], 3),
                        "bbox_y2": round(bbox[3], 3),
                        "pose_valid": int(features.valid),
                        "invalid_reason": features.invalid_reason,
                        "valid_keypoint_count": features.valid_keypoint_count,
                        "head_x": _optional(features.head_x),
                        "head_y": _optional(features.head_y),
                        "shoulder_center_x": _optional(features.shoulder_center_x),
                        "shoulder_center_y": _optional(features.shoulder_center_y),
                        "shoulder_width": _optional(features.shoulder_width),
                        "head_height_ratio": _optional(features.head_height_ratio),
                        "head_pitch_proxy_deg": _optional(features.head_pitch_proxy_deg),
                        "head_side_ratio": _optional(features.head_side_ratio),
                        "shoulder_angle_deg": _optional(features.shoulder_angle_deg),
                        "head_arm_distance_ratio": _optional(features.head_arm_distance_ratio),
                        "torso_angle_deg": _optional(features.torso_angle_deg),
                        "head_motion_ratio": _optional(decision.head_motion_ratio),
                        "candidate": int(decision.candidate),
                        "sleep_score": round(decision.sleep_score, 6),
                        "valid_ratio": round(decision.valid_ratio, 6),
                        "positive_ratio": round(decision.positive_ratio, 6),
                        "state": decision.state.value,
                        "transitioned": int(decision.transitioned),
                        "evidence": decision.reason,
                    }
                    feature_writer.writerow(row)

                    color = state_colors[decision.state]
                    label = (
                        f"T{track_id} {decision.state.value} "
                        f"score={decision.sleep_score:.2f}"
                    )
                    cv2.rectangle(
                        annotated,
                        (int(bbox[0]), int(bbox[1])),
                        (int(bbox[2]), int(bbox[3])),
                        color,
                        2,
                    )
                    cv2.putText(
                        annotated,
                        label,
                        (int(bbox[0]), max(20, int(bbox[1]) - 8)),
                        cv2.FONT_HERSHEY_SIMPLEX,
                        0.55,
                        color,
                        2,
                        cv2.LINE_AA,
                    )

                    if decision.alert:
                        alert_count += 1
                        image_name = f"sleep_track_{track_id}_{timestamp_ms}.jpg"
                        event = {
                            "control_code": args.control_code,
                            "behavior_type": "sleep",
                            "alarm_type": "sleep",
                            "track_id": track_id,
                            "frame_id": frame_count,
                            "timestamp_ms": timestamp_ms,
                            "image_path": str(output_dir / image_name),
                            "bbox_xyxy": list(bbox),
                            "sleep_score": decision.sleep_score,
                            "evidence": decision.reason,
                            "features": features.to_dict(),
                        }
                        event_stream.write(json.dumps(event, ensure_ascii=False) + "\n")
                        event_stream.flush()
                        alerts_this_frame.append(event)

                for event in alerts_this_frame:
                    cv2.imwrite(event["image_path"], annotated)

                if not args.no_video:
                    if video_writer is None:
                        height, width = annotated.shape[:2]
                        video_writer = cv2.VideoWriter(
                            str(video_path),
                            cv2.VideoWriter_fourcc(*"mp4v"),
                            fps,
                            (width, height),
                        )
                        if not video_writer.isOpened():
                            raise RuntimeError(f"cannot create output video: {video_path}")
                    video_writer.write(annotated)
                frame_count += 1
    except KeyboardInterrupt:
        print("Interrupted; writing partial results.")
    finally:
        capture.release()
        if video_writer is not None:
            video_writer.release()

    elapsed_sec = max(time.monotonic() - started_at, 1e-9)
    summary = {
        "source": args.source,
        "model": model_name,
        "frames": frame_count,
        "detections": detection_count,
        "sleep_alerts": alert_count,
        "processing_seconds": round(elapsed_sec, 3),
        "throughput_fps": round(frame_count / elapsed_sec, 3),
        "outputs": {
            "features": str(feature_path),
            "events": str(event_path),
            "annotated_video": None if args.no_video else str(video_path),
        },
        "config": config.to_dict(),
    }
    summary_path.write_text(
        json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
