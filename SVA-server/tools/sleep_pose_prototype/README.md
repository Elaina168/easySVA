# easySVA 睡岗检测原型

这是 `SVA-server` 的睡岗算法验证目录。它使用预训练 YOLO11 Pose 提取人体关键点，以肩宽归一化的头部/肩部特征和按时间计算的状态机区分“短时低头”和“持续睡岗”。同一套 ONNX 解码、特征与状态逻辑已迁入 C++ 服务，Python 原型继续作为逐视频对照基准。

本目录的正式职责是“任务一｜睡岗算法原型”。C++ 推理管线、告警和平台验证是为了验证任务一接口而提前完成的任务二成果；GB28181/ZLMediaKit、Java 后端、数据库和 Vue 前端不属于任务一。完整分工、交付状态和移交方式见 `docs/TASK1_DELIVERY_SCOPE.md`。

当前主项目中的 `BehaviorEvaluator` 保留原有“低速 + 宽高比”兼容路径；使用 `on_yolo11n_pose` 时改用 Pose 状态机判断。尚未用真实摄像头验证的阈值不应直接作为生产验收指标。

如果 Windows 本机与 Ubuntu 虚拟机分工使用，请先阅读 `docs/WINDOWS_VM_WORKFLOW.md`；代码通过 Gitee 同步，视频和结果通过共享文件夹传输。

## 1. 在 Ubuntu 22.04/WSL2 中准备环境

Ubuntu 24.04 自带的 Python 3.12 可以直接使用，不需要降级整台系统。新虚拟机先安装 Python、虚拟环境、FFmpeg 和 OpenCV 运行时：

```bash
sudo apt update
sudo apt install -y python3 python3-venv python3-pip ffmpeg libgl1

cd SVA-server/tools/sleep_pose_prototype
python3 -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip
pip install -r requirements.txt
python -m unittest discover -s tests -v
```

也可以在本目录直接执行 `bash scripts/setup_ubuntu.sh` 完成上述安装和测试。所有 Python 包都安装在 `.venv`，不会修改 Ubuntu 的系统 Python 环境。

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

已筛选的公开样本链接、许可注意事项和目标机位自采脚本见 `docs/DATA_MATERIALS.md`。原始人物视频放在 `materials/raw/`，该目录已被 Git 忽略。

## 4. 导出 ONNX

确定预训练模型和输入尺寸后执行：

```bash
python export_onnx.py \
  --model yolo11n-pose.pt \
  --imgsz 640 \
  --opset 17 \
  --output-dir models
```

命令会生成固定输入尺寸、无内置 NMS 的 ONNX，以及用 ONNX Runtime 实际读取的 `models/model_io.json`。C++ 集成必须以这个文件中的真实节点名和维度为准，不能只依据常见输出形状猜测。完整约定见 `docs/IO_CONTRACT.md`，首轮视频与 ONNX 验证记录见 `docs/BASELINE_RESULTS.md`。

## 5. 阈值调优顺序

1. 检查人框和头、肩关键点是否稳定，先解决摄像头角度、分辨率或置信度问题。
2. 对 `frame_features.csv` 中正常、短时低头、真实睡岗的特征分布做比较。
3. 调 `head_height_ratio_max`，让低头成为必要条件。
4. 调侧倾、头靠手臂、躯干前倾、肩倾和低运动量等辅助条件。
5. 最后调 `confirm_window_sec`、`sleep_positive_ratio`、`minimum_valid_ratio` 和 `recovery_sec`。

不要用训练集视频报告最终指标；按摄像头或场景拆分验证集。若不同机位的分布差异明显，应允许每路摄像头覆盖阈值，而不是训练后只保留一个全局值。

## 6. C++ 预集成状态（超出任务一最低交付范围）

为了提前验证任务一的 ONNX 与判定逻辑，当前代码已经加入独立的 C++ Pose 推理器、17 点输出、离线视频验证工具、按轨迹隔离的时序状态机和 `behavior_type=sleep` 行为接入。这部分应作为预集成成果移交给任务二负责人；GB28181 流兼容和断线重连仍由任务二完成。构建、测试和布控参数见 `docs/CPP_POSE_INTEGRATION.md`。后续验证重点是：

- 用相同视频对比 Python 与 C++ 的人框、关键点、状态跳转和事件时间，允许的数值误差需要写入验收记录。
- 使用目标机位验证双肩遮挡、多人交叉和跟踪 ID 变化时的行为。

首轮公开素材已完成程序结构、状态机和 ONNX 可执行性验证，结果见 `docs/BASELINE_RESULTS.md`。由于尚未使用目标机位采集并标注的数据，当前结果不能宣称已达到业务精度。

## 7. 联调与主项目部署注意事项

本节记录已完成的跨模块联调经验，不表示任务一负责 Java、数据库、前端或 GB28181 模块的开发与维护。

- 不要将 ONNX 权重和测试视频提交到 Git。部署模型固定放在 `/opt/SVA/models/yolo11n-pose.onnx`。
- Analyzer 需要对 `config.json` 中 `uploadDir` 下的 `alarm/` 目录具有写权限，否则告警可以入库和录像，但截图字段会为空。
- 完整 easySVA 进程栈会同时占用 Java、MySQL、Redis、ZLM 和多个 ONNX 会话的内存。虚拟机至少分配 6 GiB，推荐 8 GiB，并配置交换空间。
- 资源受限时使用 `detect_only`：关闭前端叠加和算法推流，将抽帧率设为 3–5 FPS，并避免同时启动多条验收任务。
- 后端、前端和 Analyzer 必须使用同一次功能提交构建；老数据库先执行 `SVA-backend/docs/sql/alter_h_device.sql`。

完整平台验收步骤和 2026-09-05 的联调结果见 `docs/ACCEPTANCE_CHECKLIST.md`。

## 参考项目

- [Ultralytics Pose 文档](https://docs.ultralytics.com/tasks/pose/)
- [Quectel-Pi/demo-fall-alarm-device](https://github.com/Quectel-Pi/demo-fall-alarm-device)：关键点角度与连续帧思路；使用代码前需自行核对许可。
- [Rchit13/posture-aware-fall-detection](https://github.com/Rchit13/posture-aware-fall-detection)：归一化关键点和时序窗口，MIT License。
- [bakhtiyorjondadajonov/fall-detection-vison](https://github.com/bakhtiyorjondadajonov/fall-detection-vison)：Pose、跟踪和 LSTM 的进阶路线，其许可证限制商用。
