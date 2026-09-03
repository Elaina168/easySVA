# C++ Pose 推理集成与验证

## 当前实现边界

阶段二新增了独立的 `AlgorithmOnYoloPose`，用于解析固定输出形状 `[1,56,8400]` 的 YOLO11 COCO Pose 模型。它不会复用或改变现有 `AlgorithmOnYolo` 的目标检测后处理，避免把 51 个关键点通道错误地当作类别。

当前已经完成：

- `DetectObject` 中携带 17 个 `(x, y, confidence)` 关键点；
- RGB、NCHW、`[0,1]`、居中 letterbox、填充值 114 的输入预处理；
- 人框置信度过滤、坐标逆变换和 NMS；
- `on_yolo11n_pose` 算法注册；
- 关键点加入实时检测事件 JSON；
- 服务端画面叠加 COCO 骨架；
- 独立的 `PoseSmokeTest` 视频验证程序。

睡岗特征和 15 秒状态机尚未迁移到 C++。在下一阶段完成之前，本工具只验证 C++ 人框与关键点，不代表 C++ 已经能够产生睡岗告警。

## 模型部署

`config.json` 默认模型目录是 `/opt/SVA/models`。将 Python 阶段导出的模型复制进去：

```bash
sudo mkdir -p /opt/SVA/models
sudo cp /mnt/hgfs/easySVA-share/results/sleep_pose_v0.1/yolo11n-pose.onnx \
  /opt/SVA/models/yolo11n-pose.onnx
sudo chmod 644 /opt/SVA/models/yolo11n-pose.onnx
```

服务启动时只有在文件存在时才加载 Pose 模型；文件不存在时会记录跳过日志，不影响原有算法。

## CPU 构建

```bash
cd ~/easy-sva-mo/SVA-server

cmake -S . \
  -B /tmp/easy-sva-build \
  -DCMAKE_BUILD_TYPE=Release \
  -DSVA_ONNXRUNTIME_GPU=OFF \
  -DSVA_BUILD_POSE_SMOKE_TEST=ON \
  -DONNXRUNTIME_ROOT=/usr/local/onnxruntime

cmake --build /tmp/easy-sva-build -j2
```

应同时得到：

```text
/tmp/easy-sva-build/Analyzer
/tmp/easy-sva-build/PoseSmokeTest
```

## 离线视频验证

先使用已经通过 Python 验证的 P01：

```bash
/tmp/easy-sva-build/PoseSmokeTest \
  /opt/SVA/models/yolo11n-pose.onnx \
  ~/easy-sva-mo/SVA-server/tools/sleep_pose_prototype/materials/raw/P01_sleep_desk.mp4 \
  /mnt/hgfs/easySVA-share/results/sleep_pose_v0.1/P01_cpp_pose.mp4
```

程序完成后会输出帧数、检测数、处理秒数和吞吐率。打开 `P01_cpp_pose.mp4`，检查：

1. 人框覆盖正确人物；
2. 鼻、眼、耳、肩、肘、腕等关键点位置合理；
3. 宽屏或竖屏视频中骨架没有整体上下偏移；
4. 手臂被桌面遮挡时允许低置信度点不显示，但头部和双肩应保持稳定；
5. 帧数和人物检测数与 Python 输出基本一致。

如骨架整体偏移，优先检查 letterbox 的缩放比例和左右/上下填充，不要通过调整睡岗阈值掩盖坐标错误。

## 服务算法代码

Pose 算法代码为：

```text
on_yolo11n_pose
```

`Scheduler` 从以下位置加载模型：

```text
<modelDir>/yolo11n-pose.onnx
```

在睡岗状态机接入前，暂不把生产布控切换到这个算法。下一阶段应先用离线工具对齐 Python/C++ 关键点，再迁移姿态特征和按 `streamCode + trackId` 隔离的状态机。
