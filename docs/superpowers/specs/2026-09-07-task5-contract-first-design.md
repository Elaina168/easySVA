# 任务五：契约优先的平台整合设计

状态：待用户审阅

分支：`task-5`

## 1. 目标

在不重写任务一至任务四已有算法、GB28181 协议栈和 Java 核心业务的前提下，收紧跨模块契约，完成任务五所需的前端可用性和验收闭环，并把任务一至任务四中已经暴露、会阻断任务五验收的问题一并修正。

本设计覆盖四个结果：

1. `SVA-server` 的 Pose ONNX 输入输出契约与任务一文档一致，运行时同时兼容实际出现的两种三维输出布局。
2. `SVA-backend` 的 GB28181 控制端口和平台 ID 从配置读取，不再由 `HDeviceServiceImpl` 私自固定；设备同步、在线状态和播放地址的既有幂等语义保持不变。
3. `SVA-web` 使用后端返回的设备类型、在线状态、监控状态和预览地址，补齐国标同步入口，消除协议类型混淆和示例流回退。
4. 监控墙和告警页面使用已有后端契约：监控墙只使用 `realtime` / `task`，睡岗筛选使用精确的 `alarm_type=SVA_SLEEP`。

以下内容不在本轮范围内：

- 不新增检测算法、重写 `BehaviorEvaluator` 或改变睡岗判定阈值。
- 不重写 `SVA-mediaServer` 的 GB28181 注册、收流、转码和播放实现。
- 不把缺少的真实 ONNX 权重、真实数据库、真实国标摄像机或目标机位视频伪装成已验收条件。
- 不创建新的远端分支、不推送、不合并；所有实现留在本地 `task-5`。

## 2. 已核对的现状

### 2.1 任务二的精确现状

- `SVA-server/Analyzer/Core/AlgorithmOnYoloPose.cpp` 从 ONNX Runtime 读取实际输入节点名和输出节点名，当前要求输入为固定的 `[1,3,H,W]`，输出为固定的 `[1,56,N]`。
- 同一文件使用 `cv::dnn::blobFromImage(..., true, ...)`，即把输入的 OpenCV BGR 图像交换为 RGB，再按 `1/255` 归一化成 NCHW float32 张量。
- `SVA-server/Analyzer/Core/Analyzer.cpp` 已把 `on_yolo11n_pose`、`sleep_yolopose` 和 `on_sleep_yolopose` 路由到同一个 Pose 算法实例；`SVA-server/Analyzer/Core/Config.h` 的默认模型文件名为 `yolo11n-pose.onnx`。
- `SVA-server/tools/sleep_pose_prototype/docs/IO_CONTRACT.md`、`BASELINE_RESULTS.md` 与 `SVA-server/docs/TASK2_IMPLEMENTATION.md` 对输出布局和模型文件名存在表述不一致。任务一的 `model_io.json` 未随当前仓库提供，因此实现必须以运行时实际读取的节点和维度为准，不能猜测输出节点名。

### 2.2 任务四的精确现状

- 设备接口根路径为 `/waring/device`。
- `HDeviceServiceImpl` 将 `device_type` 规范为 `rtsp` 或 `gb28181`，将 `stream_source_type` 规范为 `DIRECT` 或 `PLATFORM`。
- `HDeviceController` 已有以下接口：
  - `GET /waring/device/list`
  - `GET /waring/device/{apeId}`
  - `POST /waring/device`
  - `PUT /waring/device`
  - `POST /waring/device/monitor/{apeId}/start`
  - `POST /waring/device/monitor/{apeId}/stop`
  - `GET /waring/device/monitor/{apeId}/preview`
  - `POST /waring/device/gb28181/sync`
- 同步逻辑从 GB 控制 API 的 `/gb28181/api/devices` 和 `/gb28181/api/sessions` 读取注册设备与 `streaming` 会话，按 `gb_device_id` 幂等新增或更新，并将缺席设备置为离线。
- `HDeviceServiceImpl.fetchGbDevicesFromZlm` 当前仍固定使用控制端口 `18080` 和平台 ID `34020000002000000001`。ZLM 主机和媒体 HTTP 端口已经来自 `ZlmServer`。
- `GET /waring/device/monitor/{apeId}/preview` 返回的 `data` 当前包含 `apeId`、`name`、`streamSourceType`、`monitorStatus`、`directSourceUrl`、`playUrl`、`ipAddr`、`port` 和 `supportedMonitorStatuses`。

### 2.3 任务五前端的精确现状

- `SVA-web/src/api/device.js` 已封装设备列表、增删改、启停监控和预览，但没有封装 `/waring/device/gb28181/sync`。
- `SVA-web/src/views/device/manage.vue` 的列表列名使用 `stream_source_type`，当前显示值是 `DIRECT` / `PLATFORM`；这不能代替 `device_type` 的 `rtsp` / `gb28181`。
- `SVA-web/src/views/shipin/index.vue` 导入了 `@/api/system/kanban` 中不存在的 `setUrl`、`getTest`、`setTest`，并直接请求 `http://127.0.0.1:8080/video`。该请求不属于当前后端契约。
- `SVA-web/src/components/RTSPPlayer/index.vue`、`SVA-web/src/views/device/realtime.vue`、`SVA-web/src/views/deployment/add.vue` 和大屏组件各自实现了 RTSP/GB 地址转换，并包含 `live/acceptance` 示例地址回退。
- `SVA-web/src/views/dping/components/center-switch-panel.vue` 从监控墙为空时构造 `sourceType: 'device'` 的回退记录；但 `ScreenWallStreamServiceImpl` 只接受 `realtime` 和 `task`。
- `SVA-web/src/views/warning/index.vue` 当前只把 `alarm_type_name` 作为查询字段；`HWaringMapper.xml` 已支持 `alarm_type` 的精确条件，后端已固定使用 `SVA_SLEEP` 表示睡岗告警。

## 3. 方案选择

### 3.1 推荐：边界适配器 + 保留现有核心

在现有服务边界上补齐明确的适配器：C++ 在 ONNX 输出边界识别布局，Java 在 GB 配置边界读取配置，Vue 在 API/播放边界统一数据和地址处理。启停、同步、推理和告警的核心流程继续复用当前实现。

优点是改动集中、可用现有接口逐层验收，且可以把真实设备和模型缺失的阻塞点留到外部联调；代价是需要在前端收敛几处重复播放逻辑。

### 3.2 不采用：只修改前端显示

只把 `DIRECT`、`PLATFORM` 翻译成中文，无法解决前端没有同步入口、后端 GB 配置硬编码、监控墙发送非法 `sourceType` 和告警无法精确筛选的问题。

### 3.3 不采用：全面重写播放器和媒体服务

全面替换播放器或重写 ZLMediaKit/GB28181 适配会扩大任务五范围，引入与验收无关的媒体回归风险。本轮只抽取地址和播放条件的公共规则，保留已有 flv.js 生命周期代码。

## 4. 目标契约

### 4.1 Pose ONNX 契约

输入契约保持为：

| 项目 | 约定 |
| --- | --- |
| 图像来源 | OpenCV BGR `cv::Mat` |
| 模型输入 | `[1,3,H,W]`，固定尺寸由 ONNX 输入节点读取 |
| 预处理 | BGR 转 RGB、letterbox、填充值 114、float32、数值范围 `[0,1]` |
| 输入节点名 | 由 ONNX Runtime 实际读取，不在代码中猜测 |

输出契约改为接受以下两种布局，其他布局明确拒绝：

| 布局 | 语义 |
| --- | --- |
| `[1,56,N]` | 第二维是 56 个通道，第三维是候选数量 |
| `[1,N,56]` | 第二维是候选数量，第三维是 56 个通道 |

两种布局的 56 个通道均按现有 Pose 语义解释：4 个框值、1 个置信度和 17 组 `(x,y,confidence)`。实现先根据实际输出维度记录布局，再通过一个统一的访问函数读取 `(channel, prediction)`；不会复制两套 NMS 或坐标还原逻辑。

输出节点名仍由 ONNX Runtime 实际读取。`output0` 只能作为文档中可能出现的节点示例，不能作为代码硬编码条件。

算法代码以 `on_yolo11n_pose` 为当前主算法代码，保留 `sleep_yolopose` 和 `on_sleep_yolopose` 兼容别名；默认模型文件名统一为 `yolo11n-pose.onnx`，文档不再把 `sleep_yolopose.onnx` 描述成唯一文件名。

### 4.2 GB28181 配置契约

在 `SVA-backend/ruoyi-admin/src/main/resources/application.yml` 增加以下配置段，默认值保持当前部署行为，但值由配置文件提供：

```yaml
gb28181:
  api-port: 18080
  platform-id: 34020000002000000001
```

`HDeviceServiceImpl` 读取 `gb28181.api-port` 和 `gb28181.platform-id`，并继续从绑定的 `ZlmServer` 读取 `host` 与 `media_http_port`。同步时：

1. 使用 `http://{ZlmServer.host}:{gb28181.api-port}/gb28181/api/devices`。
2. 使用同一前缀的 `/sessions` 建立 `device_id -> stream_id` 映射，只采纳 `state=streaming` 的会话。
3. 以配置中的 `platform-id` 填充 `gb_platform_id`。
4. `media_http_port` 缺失时不拼接虚假播放地址；控制 API 请求失败时保留已有在线状态，不批量误置离线。

配置值缺失、端口非正数或平台 ID 为空时，同步接口返回可识别的错误并记录配置项名称；不再静默回退到 Java 源码中的新常量。

### 4.3 设备 API 与前端模型

后端数据库/API 的机器值保持不变：

| 字段 | 合法值或含义 | 前端展示规则 |
| --- | --- | --- |
| `device_type` | `rtsp`、`gb28181` | 展示“RTSP”或“GB28181” |
| `stream_source_type` | `DIRECT`、`PLATFORM` | 展示“直连”或“平台” |
| `is_online` | 后端当前使用字符串 `1` / `0` | 展示在线/离线 |
| `monitor_status` | `RUNNING`、`STOPPED`、`STARTING`、`STOPPING`、`ERROR` | 展示监控生命周期状态 |
| `play_url` | ZLM/后端生成的浏览器播放地址 | 只作为播放候选，不将 RTSP/GB URI 当成浏览器地址 |
| `gb_device_id` | GB 注册设备 ID | GB28181 设备显示 |
| `gb_platform_id` | 上级平台 ID | GB28181 设备显示 |

新增前端 API 函数 `syncGbDevices()`，调用 `POST /waring/device/gb28181/sync`。同步按钮执行成功后刷新 `getDeviceList`，失败时保留列表并展示后端消息；不在浏览器直接访问 GB 控制 API。

### 4.4 预览与播放地址契约

`GET /waring/device/monitor/{apeId}/preview` 的 `data.playUrl` 是设备预览的权威地址：

- RTSP DIRECT 设备的代理播放地址由后端根据已配置的 ZLM 主机、应用和媒体 HTTP 端口生成。
- GB28181 设备只使用同步得到的 `play_url`；没有活动 `play_url` 时返回空地址和设备状态，前端显示“暂无可用播放流”，不把 GB URI 或示例流当作可播放地址。
- `HDeviceServiceImpl.previewMonitor` 对 `device_type=gb28181` 不再调用 RTSP DIRECT 的 `buildDirectPlayUrl` 回退；只有普通 RTSP DIRECT 设备可以走该分支。
- 前端只接受 `http://`、`https://`、`ws://`、`wss://` 或相对媒体地址；原始 `rtsp://`、`gb://`、`gb28181://` 不能绕过预览接口直接喂给 HTML video。
- 全仓库删除 `live/acceptance` 作为运行时回退的逻辑。验收数据若需要示例地址，必须来自显式 SQL/测试夹具，而不是播放器默认值。

新增 `SVA-web/src/utils/mediaPlayback.js` 作为纯规则模块，提供：

```js
export function extractPlayableUrl(source)
export function isBrowserPlayableUrl(url)
export function isFlvUrl(url)
```

它只做字段提取和浏览器协议判断，不根据设备 ID 猜测 ZLM 地址。`RTSPPlayer`、设备实时页、布控预览页和两个大屏监控组件复用这些规则；各组件保留自己的 flv.js 实例销毁、重连和画布逻辑。

### 4.5 监控墙契约

`/screen-wall/streams` 的 `sourceType` 只允许后端现有的：

- `realtime`：设备实时流，`deviceId` 指向设备，`playUrl` 来自设备预览接口。
- `task`：布控任务流；当 `taskPushEnabled=true` 且有 `algorithmStreamUrl` 时播放算法流，否则播放任务关联设备的预览地址。

当已保存的监控墙流为空时，前端可以临时读取设备列表构造实时卡片，但必须使用 `sourceType: 'realtime'`，并逐个调用 `previewDeviceMonitor` 获取 `playUrl`。该回退只用于显示，不写入非法的 `device` 类型，也不直接使用 `direct_source_url`。

### 4.6 睡岗告警筛选契约

保留现有按 `alarm_type_name` 的通用筛选，同时增加精确的 `alarm_type` 查询字段。任务五的睡岗筛选值固定为：

```text
alarm_type=SVA_SLEEP
```

前端显示文本为“睡岗告警”，请求字段仍发送 `alarm_type`，不能把显示文本或 `alarm_type_name` 当作机器筛选值。后端继续复用 `HWaringMapper.xml` 已有的 `alarm_type = #{alarm_type}` 条件。

### 4.7 旧视频测试页

`SVA-web/src/views/shipin/index.vue` 不属于当前后端视频接口链路。为消除构建警告和错误请求：

- 删除不存在的 `setUrl`、`getTest`、`setTest` 导入和未使用的 `axios` 导入。
- 删除 `fetchData` 与 `http://127.0.0.1:8080/video` 调用。
- 保留页面文件中的静态容器和现有算法状态展示，不新增一个未定义的后端接口。

## 5. 数据流与错误处理

### 5.1 GB 同步

```text
前端同步按钮
  -> POST /waring/device/gb28181/sync
  -> HDeviceServiceImpl 读取 gb28181 配置和 ZlmServer.host
  -> GET /gb28181/api/devices + GET /gb28181/api/sessions
  -> 按 gb_device_id 幂等 upsert
  -> 只对成功返回的完整设备集合执行离线收敛
  -> 返回处理数量，前端刷新设备列表
```

控制 API 连接失败、响应为空、`code` 非零或 `data` 不是数组时，本次同步不执行离线收敛；已有设备在线状态保持不变。单个设备缺少 `device_id` 时跳过该条并记录原因。

### 5.2 设备预览

```text
页面选择设备
  -> GET /waring/device/monitor/{apeId}/preview
  -> 后端返回 playUrl 或明确的空地址
  -> mediaPlayback.extractPlayableUrl()
  -> flv.js 或原生 video 播放
```

播放失败只影响当前卡片，卡片显示“播放失败”，并释放当前播放器；不会切换到验收样例流，也不会把失败设备标记成离线。

### 5.3 启停监控

前端以 `/monitor/{apeId}/start` 和 `/monitor/{apeId}/stop` 返回的 `data.success`、`data.shortMessage` 为准，随后刷新设备行。前端不通过按钮点击结果猜测 `monitor_status`，后端返回的状态是唯一来源。

## 6. 实施边界与文件责任

任务二：

- 修改 `SVA-server/Analyzer/Core/AlgorithmOnYoloPose.cpp` 的输出布局识别和统一读取函数。
- 保持 `SVA-server/Analyzer/Core/AlgorithmOnYoloPose.h` 的公开算法接口不变；只有在保存布局状态确有必要时增加私有字段。
- 同步 `SVA-server/docs/TASK2_IMPLEMENTATION.md`、`SVA-server/tools/sleep_pose_prototype/docs/IO_CONTRACT.md` 和相关基线说明中的模型文件名、颜色顺序和布局表述。
- 为 `[1,56,N]` 与 `[1,N,56]` 增加不依赖真实权重的单元覆盖；真实 ONNX 冒烟仍以外部模型存在为前提。

任务四：

- 修改 `SVA-backend/ruoyi-admin/src/main/resources/application.yml` 增加 `gb28181.api-port` 和 `gb28181.platform-id`。
- 修改 `SVA-backend/ruoyi-admin/src/main/java/com/ruoyi/waring/service/impl/HDeviceServiceImpl.java` 读取配置并校验，删除同步方法中的两个源码常量。
- 同一文件的预览分支按 `device_type` 区分 RTSP 与 GB28181，GB28181 无 `play_url` 时返回空地址，不生成 RTSP 代理地址。
- 保持 `HDeviceController.java`、`HDeviceMapper.xml` 的既有接口和幂等逻辑；不新增数据库列。
- 增加可重复的配置/响应边界测试或纯函数测试，覆盖有效配置、无效配置、控制 API 失败保留状态三种结果。

任务五：

- 修改 `SVA-web/src/api/device.js` 增加同步调用。
- 新建 `SVA-web/src/utils/mediaPlayback.js`，统一播放字段提取和浏览器地址判断。
- 修改 `SVA-web/src/views/device/manage.vue` 显示 `device_type`、同步按钮、在线状态和 `monitor_status`。
- 修改 `SVA-web/src/views/device/realtime.vue`、`SVA-web/src/views/deployment/add.vue`、`SVA-web/src/components/RTSPPlayer/index.vue`、`SVA-web/src/views/dping/components/center-switch-panel.vue` 和 `SVA-web/src/views/dping/components/right-monitor-panel.vue`，移除示例地址和重复的地址猜测。
- 修改 `SVA-web/src/api/screenWall.js` 或其调用方，使保存与临时回退只产生 `realtime` / `task`。
- 修改 `SVA-backend/docs/sql/create_h_screen_wall_stream.sql` 的验收初始数据，将 `source_type='device'` 改为后端接受的 `source_type='realtime'`。
- 修改 `SVA-web/src/views/warning/index.vue` 增加 `alarm_type` 精确筛选，不破坏已有名称筛选。
- 修改 `SVA-web/src/views/shipin/index.vue` 清除不存在 API 的导入和 `127.0.0.1:8080/video` 调用。

## 7. 验收标准

### 7.1 静态与构建验收

- `SVA-web` 生产构建退出码为 `0`，不再出现 `setUrl`、`getTest`、`setTest` 的导入/导出警告。
- `rg` 检查前端运行时代码不再出现 `http://127.0.0.1:8080/video`、`live/acceptance` 或 `sourceType: 'device'`。
- C++ CPU 构建和 CTest 通过；布局覆盖同时验证两种 56 通道输出布局。
- Java `mvn -B test` 和 `mvn -B package` 通过；实际数据库迁移和接口运行另行记录，不以 Maven 通过替代。

### 7.2 接口验收

- 同步接口能在配置值变化后访问新的端口/平台 ID；相同 `gb_device_id` 重复同步不增加重复记录。
- 控制 API 失败时现有设备的 `is_online` 不被批量改为离线。
- RTSP 设备和 GB28181 设备在列表中显示不同的 `device_type`，且启停后 `monitor_status` 与后端一致。
- GB28181 设备有 `play_url` 时可以进入预览；没有 `play_url` 时页面显示明确不可用状态，不播放样例流。
- 监控墙实时卡片请求使用 `sourceType=realtime` 的语义，任务卡片请求使用 `sourceType=task` 的语义，后端不返回非法源类型错误。
- 睡岗筛选请求可在浏览器网络记录中看到 `alarm_type=SVA_SLEEP`，列表结果只包含该机器类型。

### 7.3 外部条件声明

下列项目必须在有真实条件时单独记录结果：真实 ONNX 权重和目标机位视频、可用 MySQL/Redis、运行中的 ZLMediaKit、真实或仓库模拟的国标设备注册、以及浏览器对实际媒体流的播放。缺少任何一项时，报告精确阻塞，不用测试夹具结果替代真实联调结论。

## 8. 交付顺序

1. 先完成任务二输出布局和文档契约，保持 C++ API 行为可回归。
2. 再完成任务四 GB 配置化和边界测试，确保同步接口不改变已有 RTSP 行为。
3. 最后完成任务五前端 API、设备页、播放器公共规则、监控墙和告警筛选。
4. 每个阶段分别构建/测试并提交；全部通过后再执行整平台运行验证。
