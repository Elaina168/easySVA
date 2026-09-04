# C++ Pose 推理集成与验证

## 当前实现边界

阶段二新增了独立的 `AlgorithmOnYoloPose`，用于解析固定输出形状 `[1,56,8400]` 的 YOLO11 COCO Pose 模型。阶段三把 Python 原型的姿态特征和四态状态机移入了服务端。Pose 推理不会复用或改变现有 `AlgorithmOnYolo` 的目标检测后处理，避免把 51 个关键点通道错误地当作类别。

当前已经完成：

- `DetectObject` 中携带 17 个 `(x, y, confidence)` 关键点；
- RGB、NCHW、`[0,1]`、居中 letterbox、填充值 114 的输入预处理；
- 人框置信度过滤、坐标逆变换和 NMS；
- `on_yolo11n_pose` 算法注册；
- 关键点加入实时检测事件 JSON；
- 服务端画面叠加 COCO 骨架；
- 独立的 `PoseSmokeTest` 视频验证程序；
- 按肩宽归一化的头、肩、手臂、躯干和运动量特征；
- 按 `streamCode + controlCode + trackId` 隔离的 `NORMAL/SUSPECT/SLEEP/RECOVER` 状态；
- `behaviorType=sleep` 复用现有行为事件链路；
- 睡岗诊断信息加入检测事件 JSON；
- 输出 `headPitchProxyDeg` 作为头部相对肩线的二维俯仰代理角（COCO 2D 关键点不能恢复真实三维俯仰角）；
- 不依赖视频和模型的 `SleepPoseUnitTest`。

当前实现仍要求双肩和至少一个头部点有效。单肩被完全遮挡时该帧记为未知观测，不会触发睡岗，也不会错误地当作已经恢复；轨迹持续丢失超过 1 秒后清除历史。这是 v0.1 的已知边界，后续应使用目标机位的遮挡样本决定是否增加单肩降级逻辑。

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
/tmp/easy-sva-build/SleepPoseUnitTest
```

先运行状态机自动测试：

```bash
ctest --test-dir /tmp/easy-sva-build --output-on-failure
```

## 离线视频验证

先使用已经通过 Python 验证的 P01：

```bash
/tmp/easy-sva-build/PoseSmokeTest \
  /opt/SVA/models/yolo11n-pose.onnx \
  ~/easy-sva-mo/SVA-server/tools/sleep_pose_prototype/materials/raw/P01_sleep_desk.mp4 \
  /mnt/hgfs/easySVA-share/results/sleep_pose_v0.1/P01_cpp_pose.mp4 \
  /mnt/hgfs/easySVA-share/results/sleep_pose_v0.1/P01_cpp_features.csv
```

程序完成后会输出帧数、检测数、睡岗告警数、首次告警毫秒、处理秒数和吞吐率，并生成逐帧特征 CSV。默认 15 秒确认窗口下，P01 预期 `sleep_alerts=1` 且 `first_sleep_alert_ms` 接近 15000。打开 `P01_cpp_pose.mp4`，检查：

1. 人框覆盖正确人物；
2. 鼻、眼、耳、肩、肘、腕等关键点位置合理；
3. 宽屏或竖屏视频中骨架没有整体上下偏移；
4. 手臂被桌面遮挡时允许低置信度点不显示，但头部和双肩应保持稳定；
5. 帧数和人物检测数与 Python 输出基本一致。

如骨架整体偏移，优先检查 letterbox 的缩放比例和左右/上下填充，不要通过调整睡岗阈值掩盖坐标错误。

P01 通过后可以批量跑输入目录中的全部 MP4：

```bash
bash tools/sleep_pose_prototype/scripts/run_cpp_baseline.sh \
  /tmp/easy-sva-build/PoseSmokeTest \
  /opt/SVA/models/yolo11n-pose.onnx \
  tools/sleep_pose_prototype/materials/raw \
  /mnt/hgfs/easySVA-share/results/sleep_pose_cpp_v0.1
```

汇总写入输出目录的 `cpp_baseline_summary.txt`，可直接与 `BASELINE_RESULTS.md` 中的 Python 告警数和首次告警时间对比。

## 服务布控

Pose 算法代码为：

```text
on_yolo11n_pose
```

`Scheduler` 从以下位置加载模型：

```text
<modelDir>/yolo11n-pose.onnx
```

布控的算法任务使用 `on_yolo11n_pose`，并配置一条 `sleep` 行为规则。以下是规则参数片段；区域 ID 和对象编码按实际项目填写：

```json
{
  "id": "sleep_pose_v01",
  "customEventName": "睡岗",
  "behaviorType": "sleep",
  "enabled": true,
  "geometryType": "region",
  "geometryId": "work_area",
  "ruleObjectCode": "person",
  "thresholdMs": 15000,
  "keypointConfidence": 0.35,
  "sleepPositiveRatio": 0.80,
  "minimumValidRatio": 0.60,
  "recoveryMs": 2000,
  "headHeightRatioMax": 0.45,
  "headSideRatioMin": 0.30,
  "headArmDistanceRatioMax": 0.75,
  "torsoAngleDegMin": 25.0,
  "shoulderTiltDegMin": 15.0,
  "motionWindowMs": 2000,
  "headMotionRatioMax": 0.15
}
```

一个布控中如有多条启用的 `sleep` 规则，当前版本由第一条规则提供状态机阈值，因此 v0.1 建议每个布控只配置一条睡岗规则。`thresholdMs` 就是从 `SUSPECT` 到 `SLEEP` 的确认窗口。

进入 `SLEEP` 的瞬间 `sleepPose.alert=true`；后续仍处在 `SLEEP/RECOVER` 时行为规则保持命中，以便现有事件生命周期持续，`alert` 不会重复。对象 JSON 的 `sleepPose` 还包含 `featuresValid`、`candidate`、`state`、`evidence`、窗口比例和各项归一化特征，便于排查阈值。
