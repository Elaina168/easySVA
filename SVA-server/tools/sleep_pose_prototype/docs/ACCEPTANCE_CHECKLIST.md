# 睡岗检测算法原型与跨任务预集成验收清单

> 范围说明：任务一的正式交付是 Python 算法原型、ONNX、输入输出说明和本地测试结果。本文中的 C++、Java、数据库与前端结果是为了验证接口而完成的额外联调证据，不应据此将 GB28181、后端或前端的后续开发责任归入任务一。详细分工见 `TASK1_DELIVERY_SCOPE.md`。

## 当前结论

任务一要求的 Python 原型、ONNX 导出、输入输出说明和本地测试已经完成。首轮 9 段公开素材用于验证程序结构和阈值边界；`P04_hand_sleeping.mp4` 仍是需要由目标机位数据继续验证的边界样本。作为超出任务一最低范围的预集成工作，C++ 推理、睡岗状态机和单元测试也已跑通；2026-09-05 进一步完成了 RTSP 场景下的 easySVA 平台联调：持续睡岗触发告警，正常写字未误报，Java 后端保存告警记录、截图和视频，原有 `on_yolo11n_80` 布控仍可产生普通行为告警。GB28181 流兼容与断线重连尚属于任务二后续工作。

## 验收项状态

| 验收项 | 归属 | 状态 | 证据或待办 |
| --- | --- | --- | --- |
| YOLO-Pose 关键点识别 | 任务一 | 已完成 | Python 可输出 17 个 COCO 关键点和标注视频；C++ 输出属于任务二预集成 |
| 头肩姿态与俯仰量 | 任务一 | 已完成（二维代理） | `headHeightRatio` 用于判定，`headPitchProxyDeg` 输出二维俯仰代理角；2D COCO 模型不能恢复真实 3D 头部姿态 |
| 多帧过滤瞬时低头 | 任务一 | 已完成 | `NORMAL/SUSPECT/SLEEP/RECOVER`，默认持续 15 秒、候选帧比例 80% |
| 本地视频输出结果 | 任务一 | 已完成首轮基线 | 公开小样本用于可执行性和边界验证；P04 仍需目标机位样本复核 |
| ONNX 导出与接口说明 | 任务一 | 已完成 | 输入 `[1,3,640,640]`，输出 `[1,56,8400]`，接口见 `IO_CONTRACT.md` |
| ONNX C++ 推理 | 任务二 | 已完成预集成 | CPU 冒烟测试通过，后续由任务二负责人维护 |
| 布控可选择睡岗 | 任务四/五 | 已完成联调 | 平台可选 `on_yolo11n_pose`、目标 `person` 和“睡岗”行为规则 |
| Analyzer 产生真实睡岗告警 | 任务二 | 已完成 RTSP 联调 | 持续睡岗视频在真实平台任务中触发告警；GB28181 流仍待任务二验证 |
| Java 入库和截图 | 任务四 | 已完成联调 | `SVA_SLEEP` 记录、截图和 ZLM 视频均可从平台访问 |
| 原有 YOLO 不受影响 | 任务二/最终联调 | 已完成 RTSP 回归 | `on_yolo11n_80` 任务产生普通“停留告警” |
| GB28181 流进入统一推理链路 | 任务二/三/四协作 | 未完成 | 任务三提供流信息、任务四下发 Analyzer 地址、任务二负责解码与重连 |

## 现场验收记录（2026-09-05）

- 正样本：`P01_sleep_desk.mp4` 在平台睡岗布控中触发 `SVA_SLEEP`，数据库记录包含可访问的 `picture_absolute_url` 和 `video_absolute_url`。
- 负样本：`N04_writing.mp4` 连续运行超过 60 秒，睡岗告警总数未增加。
- 兼容回归：普通 `on_yolo11n_80` 人员检测任务产生“停留告警”，证明原 YOLO 推理、行为规则和 Java 上报链路仍可用。
- 稳定性：小内存虚拟机曾由 Linux OOM Killer 两次终止 Analyzer；完整 easySVA 进程栈应分配至少 6 GiB 内存，推荐 8 GiB，并配置交换空间。资源受限时关闭前端叠加和算法推流，同时只运行一条验收布控。
- 精度边界：上述结论是功能验收，不替代目标机位上的独立训练集/验证集评测。

## P04 诊断与 v0.2 调整

P04 的 655 帧全部有效，因此漏报不是遮挡造成。默认 0.45 阈值下只有 180 帧成为候选，`positiveRatio` 最高为 0.5446；475 帧的原因是 `head_not_low`。该样本的 `headHeightRatio` 中位数为 0.4644。

离线回放九段 Python 特征后，将 `headHeightRatioMax` 从 0.45 调到 0.48 不会让四段负样本产生告警；用 P04 的 C++ 特征回放则预计在约 18.93 秒告警。因此 v0.2 默认值调整为 0.48。新默认值仍低于 P02 托腮直立边界样本的最小值 0.521。最终结论仍须由新版 C++ 九段批量回归确认。

## 一、虚拟机：重编译和 P04 诊断

```bash
cd ~/easy-sva-mo
git pull

cd SVA-server
cmake -S . \
  -B /tmp/easy-sva-build \
  -DCMAKE_BUILD_TYPE=Release \
  -DSVA_ONNXRUNTIME_GPU=OFF \
  -DSVA_BUILD_POSE_SMOKE_TEST=ON \
  -DONNXRUNTIME_ROOT=/usr/local/onnxruntime
cmake --build /tmp/easy-sva-build -j2
ctest --test-dir /tmp/easy-sva-build --output-on-failure
```

单独重跑 P04，并输出逐帧 CSV：

```bash
mkdir -p /mnt/hgfs/easySVA-share/results/sleep_pose_cpp_v0.2

/tmp/easy-sva-build/PoseSmokeTest \
  /opt/SVA/models/yolo11n-pose.onnx \
  ~/easy-sva-mo/SVA-server/tools/sleep_pose_prototype/materials/raw/P04_hand_sleeping.mp4 \
  /mnt/hgfs/easySVA-share/results/sleep_pose_cpp_v0.2/P04_cpp.mp4 \
  /mnt/hgfs/easySVA-share/results/sleep_pose_cpp_v0.2/P04_cpp_features.csv

python3 tools/sleep_pose_prototype/scripts/analyze_cpp_features.py \
  /mnt/hgfs/easySVA-share/results/sleep_pose_cpp_v0.2/P04_cpp_features.csv
```

判断顺序：先检查 `features_valid` 是否因双肩遮挡下降，再看 `candidate`、`positive_ratio` 是否仅略低于 0.80，最后才决定调整阈值。不要为单个样本直接降低全局阈值。

## 二、虚拟机：启动平台并创建睡岗布控

1. 确认 `/opt/SVA/models/yolo11n-pose.onnx` 存在。
2. 重新构建并启动 `sva-server`、Java 后端和 Web 前端。
3. 新建布控任务，算法选择 `YOLO11n-Pose 睡岗检测`，检测目标选择 `person`。
4. 画出工作区域，添加行为规则“睡岗”，持续时间设为 15000 ms。
5. 输入可循环播放的视频流，保证连续睡岗画面超过 20 秒。
6. 启动布控，记录任务 ID、Analyzer 日志中首次 `SLEEP` 时间和 Java 告警接口响应。

若算法下拉框不存在，先确认 Java 后端确已更新并重启；若 Analyzer 日志提示跳过 Pose 模型，检查模型路径和权限。

## 三、虚拟机：数据库和截图验收

告警产生后执行（字段以当前库为准）：

```sql
SELECT w_id,
       alarm_type,
       alarm_type_name,
       sva_behavior_type,
       picture_url,
       picture_absolute_url,
       video_url,
       sva_media_status,
       alarm_time
FROM h_waring
WHERE alarm_type = 'SVA_SLEEP'
ORDER BY w_id DESC
LIMIT 5;
```

验收必须同时满足：

- 最新记录属于本次布控，`alarm_type` 为 `SVA_SLEEP`；
- 告警名称为“睡岗告警”；
- `picture_url` 非空，对应图片文件存在且能从平台打开；
- 告警视频存在时可以播放；
- Web 告警详情中的行为类型显示为“睡岗”。

## 四、虚拟机：原 YOLO 回归

新建或复用一条原始 YOLO 布控，算法选 `on_yolo11n_80`，使用之前能稳定检测到人的视频。确认：

- Analyzer 正常加载原模型，不崩溃；
- 检测框和原有告警仍产生；
- 睡岗 Pose 模型缺失或睡岗任务停止时，不影响原任务；
- 两种算法分别启动一次后，服务没有模型输出结构混用或持续报错。

## 五、主机：保存验收材料

把以下材料统一保存在共享目录：

```text
easySVA-share/results/sleep_pose_acceptance/
├── P04_cpp.mp4
├── P04_cpp_features.csv
├── platform_sleep_alarm.png
├── database_sleep_row.png
├── saved_alarm_image.jpg
├── analyzer_sleep.log
└── original_yolo_regression.png
```

最终验收报告应给出：样本名、真实标签、是否告警、首次告警时间、误报/漏报结论、CPU 吞吐率、平台任务 ID、数据库记录 ID及截图路径。

## 六、合入主项目时的部署适配

1. ONNX 权重不提交到 Git；部署时将 `yolo11n-pose.onnx` 放入 `/opt/SVA/models/`，并确保 Analyzer 运行用户可读。
2. `/opt/SVA/config.json` 的 `uploadDir` 必须与 Nginx 的 `/alarm/` 别名指向同一上传根目录；Analyzer 运行用户必须对其 `alarm/` 子目录具有写权限。
3. 老数据库先执行 `SVA-backend/docs/sql/alter_h_device.sql`，再部署新版后端；该迁移对已存在字段使用幂等检查。
4. 依次构建并部署 Analyzer、Java 后端和前端，避免仅更新某一个组件导致算法列表、布控参数或告警字段不一致。
5. 完整进程栈建议 8 GiB 内存；内存不足时至少准备 4 GiB Swap，并关闭非验收必需的 `ws_overlay` 和推流。
6. 合入前至少执行 C++ 单元测试、Python 单元测试、一次 Pose 正/负样本回归和一次 `on_yolo11n_80` 回归。
