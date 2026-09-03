# easySVA 睡岗检测原型

这是 `SVA-server` 的独立算法验证目录。它使用预训练 YOLO11 Pose 提取人体关键点，以肩宽归一化的头部/肩部特征和按时间计算的状态机区分“短时低头”和“持续睡岗”。原型稳定后，再把 ONNX 解码和同一套状态逻辑迁入 C++。

当前主项目中的 `BehaviorEvaluator` 已有名为 `sleep` 的规则，但它主要依赖“低速 + 宽高比”，更接近躺倒检测。本目录不会改变现有服务行为，避免尚未用真实摄像头数据验证的阈值直接进入生产链路。

## 1. 在 Ubuntu 22.04/WSL2 中准备环境

建议先用 Python 3.10 或 3.11。若主机已经使用 Ubuntu 24.04/Python 3.12，也可以先尝试；依赖安装发生兼容问题时再切换到 22.04 环境，不需要先降级整台系统。

```bash
cd SVA-server/tools/sleep_pose_prototype
python3 -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip
pip install -r requirements.txt
python -m unittest discover -s tests -v
```

第一次运行时，Ultralytics 会下载 `yolo11n-pose.pt`。部署前需要确认 Ultralytics 的 AGPL-3.0 或企业授权是否符合项目的交付方式。

## 2. 先跑一段本地视频

```bash
python run_video.py \
  --source /mnt/share/videos/sleep_test.mp4 \
  --config config/default.yaml \
  --output-dir outputs/sleep_test
```

输入也可以是摄像头编号 `--source 0` 或 RTSP 地址。建议第一轮先用固定机位的视频文件，便于重复比较阈值。

输出包括：

- `annotated.mp4`：关键点以及 NORMAL/SUSPECT/SLEEP/RECOVER 状态；
- `frame_features.csv`：逐人逐帧特征，是阈值调优的主要依据；
- `events.jsonl`：进入 SLEEP 时只产生一次的告警事件；
- `sleep_track_*.jpg`：告警截图；
- `summary.json`：帧数、检测数、告警数和吞吐率。

状态机默认要求 15 秒观察窗口中至少 60% 的帧关键点有效、有效帧中至少 80% 满足睡岗姿态。关键点不可用时记为未知，不会被当成正常帧，也不会单独触发告警。

## 3. 标注和评测

复制 `examples/labels.example.csv`，按秒标注测试视频：

```csv
start_sec,end_sec,label,track_id
0,10,normal,1
10,16,short_head_down,1
16,26,normal,1
26,50,sleep,1
```

`track_id` 可留空，此时时间段作用于画面中的所有人；多人视频应填写 `frame_features.csv` 中对应人物的 ID，避免把旁人错误计入该人的标签。

然后计算帧级 precision/recall/F1 和每个睡岗片段的首次告警延迟：

```bash
python evaluate.py \
  --features outputs/sleep_test/frame_features.csv \
  --labels examples/labels.sleep_test.csv \
  --output outputs/sleep_test/evaluation.json
```

第一批样本至少应覆盖：正常值守、阅读/写字、看手机、短时低头、遮挡、趴桌、侧靠、多人交叉。先降低误报，再检查睡岗召回和告警延迟。每次只改一组阈值，并保留配置与评测结果。

## 4. 导出 ONNX

确定预训练模型和输入尺寸后执行：

```bash
python export_onnx.py \
  --model yolo11n-pose.pt \
  --imgsz 640 \
  --opset 17 \
  --output-dir models
```

命令会生成固定输入尺寸、无内置 NMS 的 ONNX，以及用 ONNX Runtime 实际读取的 `models/model_io.json`。C++ 集成必须以这个文件中的真实节点名和维度为准，不能只依据常见输出形状猜测。完整约定见 `docs/IO_CONTRACT.md`。

## 5. 阈值调优顺序

1. 检查人框和头、肩关键点是否稳定，先解决摄像头角度、分辨率或置信度问题。
2. 对 `frame_features.csv` 中正常、短时低头、真实睡岗的特征分布做比较。
3. 调 `head_height_ratio_max`，让低头成为必要条件。
4. 调侧倾、头靠手臂、躯干前倾、肩倾和低运动量等辅助条件。
5. 最后调 `confirm_window_sec`、`sleep_positive_ratio`、`minimum_valid_ratio` 和 `recovery_sec`。

不要用训练集视频报告最终指标；按摄像头或场景拆分验证集。若不同机位的分布差异明显，应允许每路摄像头覆盖阈值，而不是训练后只保留一个全局值。

## 6. C++ 集成边界

原型达到验收指标后再实施：

- 新增 Pose 专用 ONNX 解码器，不修改现有检测模型解码器去兼容两种输出；
- 给 `DetectObject` 增加 17 个关键点及置信度；
- 复用 `Scheduler` 的流处理和后端现有的 `behavior_type=sleep` 告警通道；
- 按 `track_id` 保存时序状态，把本目录的特征和状态机逻辑逐项移植；
- 用相同视频对比 Python 与 C++ 的人框、关键点、状态跳转和事件时间，允许的数值误差需要写入验收记录。

目前尚未随仓库提供真实睡岗视频，因此本目录能验证程序结构和状态机逻辑，但不能宣称已达到业务精度。真实本地测试结果需要在目标机位样本到位后生成。

## 参考项目

- [Ultralytics Pose 文档](https://docs.ultralytics.com/tasks/pose/)
- [Quectel-Pi/demo-fall-alarm-device](https://github.com/Quectel-Pi/demo-fall-alarm-device)：关键点角度与连续帧思路；使用代码前需自行核对许可。
- [Rchit13/posture-aware-fall-detection](https://github.com/Rchit13/posture-aware-fall-detection)：归一化关键点和时序窗口，MIT License。
- [bakhtiyorjondadajonov/fall-detection-vison](https://github.com/bakhtiyorjondadajonov/fall-detection-vison)：Pose、跟踪和 LSTM 的进阶路线，其许可证限制商用。
