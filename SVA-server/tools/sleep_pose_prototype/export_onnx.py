#!/usr/bin/env python3
"""Export an Ultralytics pose model and record its concrete ONNX I/O."""

from __future__ import annotations

import argparse
import json
import shutil
from pathlib import Path
from typing import Any


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Export a fixed-shape YOLO pose model to ONNX and inspect its I/O."
    )
    parser.add_argument("--model", default="yolo11n-pose.pt", help="Ultralytics .pt model")
    parser.add_argument("--output-dir", type=Path, default=Path("models"))
    parser.add_argument("--imgsz", type=int, default=640)
    parser.add_argument("--opset", type=int, default=17)
    parser.add_argument("--device", default="cpu")
    return parser.parse_args()


def _shape(value: Any) -> list[Any]:
    return [dimension if isinstance(dimension, (int, str)) else str(dimension) for dimension in value]


def main() -> int:
    args = parse_args()

    try:
        import onnxruntime as ort
        from ultralytics import YOLO
    except ImportError as exc:
        raise SystemExit(
            "Missing export dependencies. Run: pip install -r requirements.txt"
        ) from exc

    if args.imgsz <= 0:
        raise SystemExit("--imgsz must be positive")

    args.output_dir.mkdir(parents=True, exist_ok=True)
    model = YOLO(args.model)
    exported = Path(
        model.export(
            format="onnx",
            imgsz=args.imgsz,
            batch=1,
            dynamic=False,
            simplify=True,
            opset=args.opset,
            nms=False,
            device=args.device,
        )
    ).resolve()

    target = (args.output_dir / exported.name).resolve()
    if exported != target:
        shutil.copy2(exported, target)

    session = ort.InferenceSession(str(target), providers=["CPUExecutionProvider"])
    contract = {
        "model": str(target),
        "export": {
            "imgsz": args.imgsz,
            "batch": 1,
            "dynamic": False,
            "opset": args.opset,
            "nms": False,
        },
        "inputs": [
            {"name": item.name, "shape": _shape(item.shape), "type": item.type}
            for item in session.get_inputs()
        ],
        "outputs": [
            {"name": item.name, "shape": _shape(item.shape), "type": item.type}
            for item in session.get_outputs()
        ],
        "preprocessing": {
            "layout": "NCHW",
            "color": "RGB",
            "dtype": "float32",
            "range": [0.0, 1.0],
            "resize": "aspect-ratio-preserving letterbox",
            "padding_value": 114,
        },
        "notes": [
            "Use these inspected names and shapes in C++; do not hard-code a guessed output name.",
            "A typical COCO pose raw output is [1, 56, 8400]: 4 box values, 1 class confidence, and 17 keypoints x (x, y, confidence).",
            "Coordinates must be transformed back with the exact letterbox scale and padding used during preprocessing.",
        ],
    }
    contract_path = args.output_dir / "model_io.json"
    contract_path.write_text(
        json.dumps(contract, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )

    print(f"ONNX: {target}")
    print(f"I/O contract: {contract_path.resolve()}")
    print(json.dumps(contract, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
