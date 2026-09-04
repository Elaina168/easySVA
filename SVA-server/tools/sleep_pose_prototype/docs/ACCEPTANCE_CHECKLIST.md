# 睡岗检测算法原型与推理集成验收清单

## 当前结论

截至 C++ v0.1 基线，Python 原型、ONNX 导出、C++ 推理、睡岗状态机和单元测试已经跑通。9 段基线视频中 C++ 与 Python 有 8 段行为结论一致；`P04_hand_sleeping.mp4` 是唯一待解释的 C++ 漏报。平台真实任务产生告警、Java 接收入库、截图落盘及原有 YOLO 运行回归仍需完成，这几项不能用离线冒烟测试替代。

## 验收项状态

| 验收项 | 状态 | 证据或待办 |
| --- | --- | --- |
| YOLO-Pose 关键点识别 | 已完成 | C++/Python 均可输出 17 个 COCO 关键点和标注视频 |
| 头肩姿态与俯仰量 | 已完成（二维代理） | `headHeightRatio` 用于判定，`headPitchProxyDeg` 输出二维俯仰代理角；2D COCO 模型不能恢复真实 3D 头部姿态 |
| 多帧过滤瞬时低头 | 已完成 | `NORMAL/SUSPECT/SLEEP/RECOVER`，默认持续 15 秒、候选帧比例 80% |
| 本地视频输出结果 | 基本完成 | 正样本 3/4 触发，负样本 4/4 不误报；P04 待诊断 |
| ONNX 导出与 C++ 推理 | 已完成 | 输入 `[1,3,640,640]`，输出 `[1,56,8400]`，CPU 冒烟测试通过 |
| 布控可选择睡岗 | 代码已补，待部署验证 | 后端补充内置 `on_yolo11n_pose` 项，目标为 `person`；前端行为选择显示“睡岗” |
| Analyzer 产生真实睡岗告警 | 待验收 | 必须用平台任务和持续睡岗视频运行一次 |
| Java 入库和截图 | 待验收 | 验证 `h_waring.alarm_type=SVA_SLEEP`、`picture_url` 非空且图片真实存在 |
| 原有 YOLO 不受影响 | 待验收 | 至少运行一条原 `on_yolo11n_80` 任务并确认检测和告警正常 |

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
SELECT w_id, alarm_type, alarm_name, picture_url, video_url, create_time
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
