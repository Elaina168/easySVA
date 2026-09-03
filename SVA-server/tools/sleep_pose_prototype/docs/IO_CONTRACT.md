# 睡岗 Pose 模型与行为输出约定

版本：原型 v0.1

本文把“模型张量接口”和“睡岗业务判定”分开描述。ONNX 只负责人和关键点；睡岗状态由可解释的特征及多帧逻辑产生。

## ONNX 输入

| 项目 | 约定 |
| --- | --- |
| 批次 | 固定 1 |
| 尺寸 | `[1, 3, 640, 640]`，实际值以 `model_io.json` 为准 |
| 类型 | `float32` |
| 布局 | NCHW |
| 颜色 | RGB |
| 数值范围 | `[0, 1]`，即 uint8 除以 255 |
| 缩放 | 保持宽高比的 letterbox |
| 填充值 | 114 |

预处理必须记录 `scale`、`pad_x` 和 `pad_y`，用于把人框与关键点映射回原图。Python 与 C++ 必须使用相同的插值、取整和填充规则。

## ONNX 输出

导出参数为固定尺寸、`nms=False`。常见 COCO Pose 原始输出是 `[1, 56, 8400]`：

- 4 个边界框值；
- 1 个 person 类别置信度；
- 17 × 3 个关键点值 `(x, y, confidence)`。

这一形状只是常见示例，不能视作硬编码契约。每次替换权重或 Ultralytics 版本后都要运行 `export_onnx.py`，以生成的 `model_io.json` 作为节点名称、维度和类型的权威记录。后处理负责置信度过滤、xywh/xyxy 转换、NMS、letterbox 逆变换以及越界裁剪。

## COCO 17 关键点编号

| 编号 | 名称 | 原型用途 |
| ---: | --- | --- |
| 0 | nose | 头部中心 |
| 1 | left_eye | 头部中心 |
| 2 | right_eye | 头部中心 |
| 3 | left_ear | 头部中心 |
| 4 | right_ear | 头部中心 |
| 5 | left_shoulder | 肩中心、肩宽、肩倾 |
| 6 | right_shoulder | 肩中心、肩宽、肩倾 |
| 7 | left_elbow | 头靠手臂距离 |
| 8 | right_elbow | 头靠手臂距离 |
| 9 | left_wrist | 头靠手臂距离 |
| 10 | right_wrist | 头靠手臂距离 |
| 11 | left_hip | 躯干方向（可选） |
| 12 | right_hip | 躯干方向（可选） |
| 13 | left_knee | 当前不使用 |
| 14 | right_knee | 当前不使用 |
| 15 | left_ankle | 当前不使用 |
| 16 | right_ankle | 当前不使用 |

坐标单位是原始图像像素。关键点置信度小于 `keypoint_confidence` 时视为无效。

## 特征与有效性

两个肩点和至少一个头部点有效，才计算睡岗特征。下半身可以被桌面遮挡；髋部不可见只会让躯干角度为空。肩宽小于 1 像素视为无效。

核心特征：

```text
shoulder_center = (left_shoulder + right_shoulder) / 2
head_center = confidence-weighted mean(nose, eyes, ears)
head_height_ratio = (shoulder_center.y - head_center.y) / shoulder_width
head_side_ratio = abs(head_center.x - shoulder_center.x) / shoulder_width
head_arm_distance_ratio = min(distance(head_center, visible elbows/wrists)) / shoulder_width
```

肩宽归一化使阈值对人物远近更稳健。单帧候选要求“头部较低”并至少满足一项辅助证据：侧倾、头靠手臂、躯干前倾、肩倾或低运动量。

关键点无效时状态为未知观测：不触发睡岗，也不能用来证明人物已经恢复正常。轨迹消失超过 `tracker_max_missing_sec` 时清除该人的历史。

## 时序状态

```text
NORMAL --候选姿态--> SUSPECT --持续满足窗口条件--> SLEEP
SLEEP --正常姿态--> RECOVER --持续正常 recovery_sec--> NORMAL
RECOVER --再次候选--> SLEEP（同一事件，不重复告警）
```

进入 `SLEEP` 的瞬间产生一次告警。默认确认窗口 15 秒，候选占有效帧至少 80%，有效帧占窗口至少 60%。状态机使用毫秒时间戳，而不是固定帧数，因此视频掉帧时持续时间语义仍然一致。

## 原型文件输出

`frame_features.csv` 每行对应某帧中的一个人物，包含：帧号、毫秒/秒时间戳、track ID、人框、关键点有效性、各归一化特征、窗口比例、候选标记、状态和证据文本。

`events.jsonl` 每行是一个告警对象，主要字段如下：

```json
{
  "control_code": "prototype",
  "behavior_type": "sleep",
  "alarm_type": "sleep",
  "track_id": 1,
  "frame_id": 725,
  "timestamp_ms": 29000,
  "image_path": "outputs/test/sleep_track_1_29000.jpg",
  "bbox_xyxy": [100.0, 50.0, 300.0, 420.0],
  "sleep_score": 0.84,
  "evidence": "head_low+head_near_arm+low_motion",
  "features": {}
}
```

后端已有 `behavior_type=sleep` / `alarm_type=sleep` 的接收约定，所以 C++ 阶段继续沿用 `sleep`，不另造业务枚举。

## C++ 接入检查表

1. 新建 `AlgorithmOnYoloPose`，按实际 `model_io.json` 解析 Pose 输出。
2. 扩展检测结果结构，使每个人框携带 17 个 `(x, y, confidence)`。
3. 确认人框 NMS 后关键点仍与对应人物保持同一索引。
4. 把 track ID、时间戳、Pose 特征放入每路视频独立的时序上下文。
5. 移植 NORMAL/SUSPECT/SLEEP/RECOVER 状态机并读取规则参数。
6. 告警沿用现有 `sleep` 通道，保存告警截图和可追踪的阈值版本。
7. 对同一视频逐帧比较 Python/C++ 输出，重点检查 letterbox 逆变换、NMS、跟踪 ID 和首次告警时间。
