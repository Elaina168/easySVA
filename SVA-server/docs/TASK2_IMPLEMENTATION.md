# 任务二：SVA-server 推理服务开发与交付说明

## 一、任务背景与职责说明

根据企业应用设计实践项目“验收点2-分工任务组”的模块所有权划分：
- **任务定位**：任务二｜SVA-server C++ 推理服务
- **负责模块**：`SVA-server`（C++ 高性能视频分析与推理引擎）
- **核心交付目标**：
  1. **接入任务一睡岗 ONNX 模型**：接入睡岗检测模型（YOLO-Pose / `sleep_yolopose`），完成 C++ 推理管线对接、17点姿态后处理、双重判定逻辑（姿态轴倾角优先 + 框宽高比回退）、时序平滑及告警事件上报。
  2. **兼容 GB28181 视频流地址**：全面兼容 `gb28181://<host>[:<port>]/<app>/<stream>` 与 `gb://<host>/<app>/<stream>` 协议地址，将国标流与原有 RTSP 流统一接入底层解码拉流管线，支持 SIP/RTP 协商建链延迟重试。
  3. **监控流稳定性与全链路联调**：解决长时间播放 MediaSource 缓冲区堆积断流、修复监控墙缺少表报错、提供多分屏实时预览工作台。

---

## 二、架构设计与核心实现

### 1. 睡岗模型推理与行为判定管线（YOLO-Pose）
- **算法路由接入**：`Analyzer.cpp` 将 `on_yolo11n_pose` 作为主算法代码，并保留 `sleep_yolopose` 与 `on_sleep_yolopose` 兼容别名。
- **ONNX Runtime 引擎加载**：
  - 支持 GPU（CUDA）硬件加速优先，无 GPU 时自动回退至 CPUExecutionProvider。
  - 默认加载 `modelDir/yolo11n-pose.onnx`，可由 `sleepModelFile` 指定文件名；仅在默认文件不可用时兼容查找 `sleep_yolopose.onnx`。模型未安装时记录提示信息，不影响原有 YOLO11/YOLO26 任务的稳定运行。
- **姿态关键点与睡岗逻辑判定**（`SleepPoseEvaluator.cpp`）：
  - 支持 Ultralytics YOLO-Pose 标准输出格式：`[1, 56, N]`（4 坐标 + 1 置信度 + 17×3 关键点）以及转置输出 `[1, N, 56]`；布局由 ONNX Runtime 实际输出维度识别，不按节点名称猜测。
  - 提取左右双肩（点5/6）到左右双髋（点11/12）的人体躯干中心轴向量，计算躯干与垂直方向的倾角 $\theta$。当 $|\theta| > 60^\circ$ 时判定为人体横向躺卧睡岗。
  - 关键点置信度不足或遮挡时，平滑回退至目标边界框宽高比（$w/h > 1.3$）与低头沉睡判定。
  - 结合时间窗口约束（连续处于判定状态超过 `thresholdMs` 且位移在 `maxDisplacementPx` 范围内），触发正式睡岗事件并随 `detect.frame` 推送 WebSocket。

### 2. GB28181 国标协议流地址归一化兼容
- **统一地址规范化（`normalizeStreamUrl`）**（`AvPullStream.cpp`）：
  - 识别 `gb28181://<host>[:<port>]/<app>/<stream>` 及简写 `gb://...` 格式。
  - 自动将国标逻辑地址映射为底层媒体服务（ZLMediaKit）的标准 RTSP 端口（默认 9994）拉流地址，使 GB28181 与 RTSP 流统一进入同一拉流与解码线程。
  - 保留原有 `rtsp://`、`rtmp://`、`http-flv` / `hls` 协议的完全向后兼容。
- **建链延迟探测与平滑重试**：
  - 针对国标设备在 SIP INVITE 与 RTP 推流建链期间存在的 1~3 秒时间差，初始探测最大重试扩充至 5 次，并在探测退避期间避免误报连接失败。
- **API 入参解析健壮性**（`Server.cpp`）：
  - `/api/control/add`、`/api/control/cancel` 等接口增加安全类型校验，避免未传属性时调用 `asCString()` 抛出 `Json::LogicError` 异常导致进程退出。
  - 支持从流地址协议头自动推导 `streamProtocol: "gb28181"`。

### 3. 前端监控台与流媒体长连接优化
- **流媒体协议解析与自动回收缓存**：
  - `RTSPPlayer/index.vue` 与 `realtime.vue` 统一支持国标地址转换低延迟 FLV 流播放。
  - 开启 `autoCleanupSourceBuffer: true`，将浏览器内存缓存限制在最近 15 秒，彻底解决长时间播放数千帧后 MediaSource 内存爆满导致“视频流断开”的问题。
  - 增加 1.2 秒守护式无感自动重连，网络微抖动后自动恢复。
- **大屏电视墙数据表补齐**：
  - 增加 `h_screen_wall_stream` 数据库表，解决大屏实时监控报错。

---

## 三、验证方法与实测结果

### 1. 自动化单元测试验证
执行测试套件命令：
```bash
export LD_LIBRARY_PATH="/home/sugaria/easySVA-Mo/runtime/user/usr/lib:/home/sugaria/easySVA-Mo/runtime/user/usr/lib64:/home/sugaria/easySVA-Mo/runtime/user/usr/lib/x86_64-linux-gnu:/home/sugaria/easySVA-Mo/runtime/onnxruntime-linux-x64-1.20.1/lib:$LD_LIBRARY_PATH"

./SVA-server/build/StreamUrlUnitTest
./SVA-server/build/SleepPoseUnitTest
```
**实测结果**：
- `StreamUrlUnitTest`: 5 组用例（标准 GB28181、自定义端口、简写 `gb://`、原生 RTSP、HTTP-FLV）全部 **[PASS]**。
- `SleepPoseUnitTest`: 睡岗横躺判定、关键点低置信度回退、时序静止停留约束全部 **[PASS]**。

### 2. C++ 分析器接口端到端验证
通过 `POST /api/control/add` 向分析器直接发送带有国标协议的布控任务：
```bash
curl -s -X POST http://127.0.0.1:9993/api/control/add \
  -H "Content-Type: application/json" \
  -d '{
    "code": "gb-test-01",
    "streamCode": "test-device",
    "streamApp": "live",
    "streamName": "acceptance",
    "streamUrl": "gb28181://127.0.0.1:9994/live/acceptance",
    "pushStream": false,
    "renderMode": "ws_overlay",
    "algorithmCode": "on_yolo11n_80",
    "objectCodes": ["person"],
    "recognitionRegion": "0,0,1,0,1,1,0,1",
    "algorithmTasks": [{"algorithmCode":"on_yolo11n_80","objectCodes":["person"],"detectFps":5}]
  }'
```
**日志核验证据（`runtime/logs/analyzer.log`）**：
```text
[INFO] [connect:67] GB28181 source normalized for unified decoder: gb28181://127.0.0.1:9994/live/acceptance -> rtsp://127.0.0.1:9994/live/acceptance
[INFO] [connect:163] CUDA hardware decoding ENABLED for stream: rtsp://127.0.0.1:9994/live/acceptance
```
调用 `POST /api/control/add` 返回任务状态为 1000（成功），`checkFps` 与 `detectFps` 稳定工作。

### 3. Web 网页端全流程验证
1. **设备管理（`http://localhost:8080/#/device`）**：
   - 查看或新增设备，输入 `gb28181://127.0.0.1:9994/live/acceptance`。
   - 列表中清晰展示“在线状态”与“监控状态”。
   - 点击“预览视频”，播放器自动解析并实时播放视频。
2. **实时预览工作台（`http://localhost:8080/#/device/realtime`）**：
   - 支持 1/4/9 分屏切换。
   - 点击国标测试摄像头，画面流畅呈现，支持长连接稳定播放。
3. **布控管理（`http://localhost:8080/#/deployment`）**：
   - 现有的 `acceptance-camera-yolo` 处于运行中状态，点击“预览”可查看实时检测框与告警记录。

---

## 四、任务一睡岗模型交付与对接规范

将任务一导出的 ONNX 模型放置在 `modelDir` 目录（默认文件名 `runtime/models/yolo11n-pose.onnx`）。
若使用自定义文件名，可在 `sva-server.json` 中配置 `sleepModelFile`，无需重新编译分析器；旧文件名 `sleep_yolopose.onnx` 仅作为兼容候选，不是唯一契约。

- **输入规格**：保持 `[1, 3, H, W]`，输入图像来源是 OpenCV BGR；letterbox 后由 `blobFromImage(..., swapRB=true, 1/255)` 形成 RGB/NCHW `float32` 张量。具体固定尺寸由运行时输入节点读取。
- **输出规格**：只接受 `[1, 56, N]` 或 `[1, N, 56]`（4 坐标 + 1 人体置信度 + 17×3 姿态关键点）；具体 `N`、输入节点名和输出节点名由 ONNX Runtime 实际读取。
- **示例布控结构**：
```json
{
  "algorithmCode": "sleep_yolopose",
  "algorithmTasks": [{
    "algorithmCode": "sleep_yolopose",
    "objectCodes": ["person"],
    "detectFps": 8,
    "scoreThreshold": 0.25,
    "nmsThreshold": 0.45
  }],
  "behaviorRules": [{
    "id": "sleep_rule_1",
    "behaviorType": "sleep",
    "ruleObjectCode": "person",
    "geometryId": "region_primary",
    "enabled": true,
    "thresholdMs": 15000,
    "maxSpeedPxPerSec": 6,
    "maxDisplacementPx": 48
  }]
}
```
