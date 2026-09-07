# 任务五契约收紧 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在本地 `task-5` 分支完成任务二、任务四和任务五中已经暴露、会阻断平台验收的契约修正，并让 C++ 推理、GB28181 同步/预览和 Vue 播放链路可以分层验证。

**Architecture:** 保留现有推理、设备同步、监控墙和播放器的核心流程，只在边界增加可测试的适配器。C++ 用一个输出布局描述和统一访问器兼容 `[1,56,N]` 与 `[1,N,56]`；Java 用配置字段和显式校验替代 GB28181 硬编码；Vue 用一个纯规则模块提取后端播放地址并拒绝原始 RTSP/GB URI，所有监控墙源类型收敛为 `realtime` 或 `task`。

**Tech Stack:** C++17、CMake/CTest、ONNX Runtime、OpenCV；Java 17、Spring Boot、MyBatis、JUnit 5/Mockito；Vue 2、Element UI、flv.js、Vue CLI。

**Spec:** `docs/superpowers/specs/2026-09-07-task5-contract-first-design.md`

## Global Constraints

- 仅在本地 `task-5` 分支工作；不创建远端分支、不推送、不合并、不部署真实服务。
- 不重写已有检测算法、`BehaviorEvaluator`、GB28181 注册/收流协议栈或 ZLMediaKit 播放实现。
- 输入保持 `[1,3,H,W]`；Pose 输出只接受 `[1,56,N]` 和 `[1,N,56]`，节点名称由 ONNX Runtime 运行时读取。
- `gb28181.api-port` 使用 `18080`，`gb28181.platform-id` 使用 `34020000002000000001`；源码中不保留这两个同步常量。
- 后端机器值保持 `device_type=rtsp|gb28181`、`stream_source_type=DIRECT|PLATFORM`、`is_online=1|0`；前端不得用一个字段代替另一个字段。
- 浏览器播放地址只接受 `http://`、`https://`、`ws://`、`wss://` 或相对媒体地址；不从设备 ID、RTSP URI 或 `live/acceptance` 推导地址。
- GB28181 没有有效 `play_url` 时只显示不可播放状态；同步控制 API 失败时不得批量把已有设备置为离线。
- 保留已有未跟踪的 `docs/2026-09-07-full-platform-verification-record.md` 和两个 `__pycache__` 目录，不在本计划中删除或提交。
- 所有新增/修改文件使用 UTF-8；代码注释使用中文；提交前执行 `git diff --check`。

## File Map

### 任务二：Pose 输出边界

- Create: `SVA-server/Analyzer/Core/PoseOutputLayout.h` — 只依赖标准库的三维输出布局识别和统一元素访问器。
- Create: `SVA-server/Analyzer/Tools/PoseOutputLayoutUnitTest.cpp` — 用合成张量覆盖两种合法布局和非法布局。
- Modify: `SVA-server/Analyzer/Core/AlgorithmOnYoloPose.h` — 保存运行时识别出的布局状态，不改变 `AlgorithmOnYoloPose::objectDetect` 公开接口。
- Modify: `SVA-server/Analyzer/Core/AlgorithmOnYoloPose.cpp` — 从实际输出维度识别布局，运行时用同一访问器读取框、置信度和关键点。
- Modify: `SVA-server/CMakeLists.txt` — 注册 `PoseOutputLayoutUnitTest` CTest 目标。
- Modify: `SVA-server/docs/TASK2_IMPLEMENTATION.md` — 更正模型文件名、颜色顺序和双布局契约，删除把 `sleep_yolopose.onnx` 当作唯一文件名的表述。
- Modify: `SVA-server/tools/sleep_pose_prototype/docs/IO_CONTRACT.md` — 明确 `[1,56,N]` 与 `[1,N,56]` 是允许的布局，节点/维度以运行时模型记录为准。
- Modify: `SVA-server/tools/sleep_pose_prototype/docs/BASELINE_RESULTS.md` — 将当前实测模型文件、输入 RGB/NCHW 预处理和输出布局写成与 C++ 实现一致的交付说明。

### 任务四：GB28181 配置和设备预览边界

- Modify: `SVA-backend/ruoyi-admin/src/main/resources/application.yml` — 增加 `gb28181.api-port` 和 `gb28181.platform-id`。
- Modify: `SVA-backend/ruoyi-admin/src/main/java/com/ruoyi/waring/service/impl/HDeviceServiceImpl.java` — 注入并校验配置；同步 URL 使用配置端口/平台 ID；缺少媒体 HTTP 端口时不生成播放地址；GB28181 预览不走 RTSP DIRECT 回退。
- Modify: `SVA-backend/ruoyi-admin/pom.xml` — 仅为可重复的服务边界单元测试增加 `spring-boot-starter-test` 的 test 依赖。
- Create: `SVA-backend/ruoyi-admin/src/test/java/com/ruoyi/waring/service/impl/HDeviceServiceImplTest.java` — 覆盖有效配置、无效配置、控制 API 失败保持原状态、媒体端口缺失和 GB 预览无回退。

### 任务五：前端、监控墙和睡岗筛选

- Modify: `SVA-web/src/api/device.js` — 增加 `syncGbDevices()`，浏览器不直连 GB 控制 API。
- Create: `SVA-web/src/utils/mediaPlayback.js` — 提供 `extractPlayableUrl(source)`、`isBrowserPlayableUrl(url)`、`isFlvUrl(url)` 三个纯函数。
- Modify: `SVA-web/src/views/device/manage.vue` — 分开显示协议类型/流来源/在线状态/监控状态，加入 GB 同步按钮，预览只使用后端 `playUrl`。
- Modify: `SVA-web/src/views/device/realtime.vue` — 删除 RTSP/GB 地址猜测，预览和监控墙写入只接受浏览器可播放地址。
- Modify: `SVA-web/src/views/deployment/add.vue` — 预览只使用后端地址；失败或无地址时清理播放器并显示明确提示。
- Modify: `SVA-web/src/components/RTSPPlayer/index.vue` — 复用 `mediaPlayback`，删除 `live/acceptance` 和协议地址转换回退。
- Modify: `SVA-web/src/views/dping/components/center-switch-panel.vue` — 监控墙空配置临时卡片使用 `sourceType: 'realtime'`，逐个请求设备预览，不使用 `direct_source_url`。
- Modify: `SVA-web/src/views/dping/components/right-monitor-panel.vue` — 复用播放地址规则并拒绝原始协议地址。
- Modify: `SVA-web/src/api/screenWall.js` — 只向后端构造 `realtime`/`task`，拒绝非法源类型。
- Modify: `SVA-web/src/views/warning/index.vue` — 增加机器字段 `alarm_type` 的睡岗选项，固定值 `SVA_SLEEP`，保留 `alarm_type_name` 通用筛选。
- Modify: `SVA-web/src/views/shipin/index.vue` — 删除不存在的 API 导入、`axios` 导入和 `http://127.0.0.1:8080/video` 请求，保留静态容器及算法状态展示。
- Modify: `SVA-backend/ruoyi-admin/src/main/resources/mapper/waring/HWaringMapper.xml` — 为非管理员按组织查询补齐已有的 `alarm_type = #{alarm_type}` 条件，确保 `/waring/waring/list?alarm_type=SVA_SLEEP` 在两条权限路径上语义一致。
- Modify: `SVA-backend/docs/sql/create_h_screen_wall_stream.sql` — 将验收监控墙源类型从 `device` 改为 `realtime`，保留显式 SQL 夹具中的地址，不把该地址放回运行时播放器回退。

---

### Task 1: 任务二 Pose 输出布局适配

**Files:**

- Create: `SVA-server/Analyzer/Core/PoseOutputLayout.h`
- Create: `SVA-server/Analyzer/Tools/PoseOutputLayoutUnitTest.cpp`
- Modify: `SVA-server/Analyzer/Core/AlgorithmOnYoloPose.h`
- Modify: `SVA-server/Analyzer/Core/AlgorithmOnYoloPose.cpp`
- Modify: `SVA-server/CMakeLists.txt`
- Modify: `SVA-server/docs/TASK2_IMPLEMENTATION.md`
- Modify: `SVA-server/tools/sleep_pose_prototype/docs/IO_CONTRACT.md`
- Modify: `SVA-server/tools/sleep_pose_prototype/docs/BASELINE_RESULTS.md`

**Interfaces:**

- Consumes: ONNX Runtime 第一个输入/输出节点的运行时名称和三维输出 shape；现有 `PoseOnnxRuntimeEngine::runInference` 的 float tensor。
- Produces: `PoseOutputShape inspectPoseOutputShape(const std::vector<int64_t>& dims)` 和 `float poseOutputValue(const float* data, const PoseOutputShape& shape, int channel, int prediction)`；`AlgorithmOnYoloPose::objectDetect` 签名不变。

- [ ] **Step 1: Write the failing layout test**

  在 `PoseOutputLayoutUnitTest.cpp` 写成独立可执行测试。合成 `predictionCount=2` 的张量时，使用 `channel * 100.0f + prediction` 作为 `[1,56,N]` 的值；使用 `prediction * 100.0f + channel` 作为 `[1,N,56]` 的物理存储值。测试必须读取 `channel=0,prediction=1`、`channel=4,prediction=0`、`channel=55,prediction=1`，并断言两种布局返回同一语义值；再断言 `{1,55,2}`、`{1,2,55}`、`{2,56,2}` 和四维 shape 抛出 `std::invalid_argument`。

  ```cpp
  #include "PoseOutputLayout.h"

  #include <cmath>
  #include <cstdio>
  #include <stdexcept>
  #include <vector>

  namespace {
      int failures = 0;

      void expect(bool condition, const char *message)
      {
          if (!condition)
          {
              std::fprintf(stderr, "FAIL: %s\n", message);
              ++failures;
          }
      }

      void expectThrows(const std::vector<int64_t> &dims, const char *message)
      {
          try
          {
              SVAAnalyzer::inspectPoseOutputShape(dims);
              expect(false, message);
          }
          catch (const std::invalid_argument &)
          {
              expect(true, message);
          }
      }
  }

  int main()
  {
      const int predictionCount = 2;
      std::vector<float> channelsFirst(56 * predictionCount);
      std::vector<float> predictionsFirst(56 * predictionCount);
      for (int channel = 0; channel < 56; ++channel)
      {
          for (int prediction = 0; prediction < predictionCount; ++prediction)
          {
              channelsFirst[channel * predictionCount + prediction] =
                  static_cast<float>(channel * 100 + prediction);
              predictionsFirst[prediction * 56 + channel] =
                  static_cast<float>(channel * 100 + prediction);
          }
      }

      const auto channelsFirstShape = SVAAnalyzer::inspectPoseOutputShape({1, 56, predictionCount});
      const auto predictionsFirstShape = SVAAnalyzer::inspectPoseOutputShape({1, predictionCount, 56});
      for (int channel : {0, 4, 55})
      {
          for (int prediction = 0; prediction < predictionCount; ++prediction)
          {
              const float expected = static_cast<float>(channel * 100 + prediction);
              expect(std::abs(SVAAnalyzer::poseOutputValue(channelsFirst.data(), channelsFirstShape, channel, prediction) - expected) < 0.001f,
                     "channels-first value keeps channel/prediction semantics");
              expect(std::abs(SVAAnalyzer::poseOutputValue(predictionsFirst.data(), predictionsFirstShape, channel, prediction) - expected) < 0.001f,
                     "predictions-first value keeps channel/prediction semantics");
          }
      }

      expectThrows({1, 55, 2}, "55-channel output is rejected");
      expectThrows({1, 2, 55}, "transposed 55-channel output is rejected");
      expectThrows({2, 56, 2}, "batch size other than one is rejected");
      expectThrows({1, 56, 2, 1}, "non-three-dimensional output is rejected");
      return failures == 0 ? 0 : 1;
  }
  ```

- [ ] **Step 2: Run the new test before implementation and verify it fails**

  从仓库根目录执行：

  ```bash
  cmake -S SVA-server -B SVA-server/build -DBUILD_TESTING=ON -DSVA_BUILD_POSE_SMOKE_TEST=OFF -DSVA_ONNXRUNTIME_GPU=OFF
  cmake --build SVA-server/build --target PoseOutputLayoutUnitTest
  ```

  预期失败：当前 CMake 没有 `PoseOutputLayoutUnitTest` target，且 `PoseOutputLayout.h` 尚不存在；不能把该失败当作依赖缺失以外的代码结论。

- [ ] **Step 3: Implement the pure layout adapter**

  在 `PoseOutputLayout.h` 中定义以下无 ONNX 依赖的接口：

  ```cpp
  namespace SVAAnalyzer {
      enum class PoseOutputLayout {
          ChannelsFirst,
          PredictionsFirst
      };

      struct PoseOutputShape {
          PoseOutputLayout layout;
          int channels;
          int predictionCount;
      };

      inline PoseOutputShape inspectPoseOutputShape(const std::vector<int64_t> &dims)
      {
          if (dims.size() != 3 || dims[0] != 1)
          {
              throw std::invalid_argument("Pose output must have shape [1,56,N] or [1,N,56]");
          }
          if (dims[1] == 56 && dims[2] > 0)
          {
              return {PoseOutputLayout::ChannelsFirst, 56, static_cast<int>(dims[2])};
          }
          if (dims[2] == 56 && dims[1] > 0)
          {
              return {PoseOutputLayout::PredictionsFirst, 56, static_cast<int>(dims[1])};
          }
          throw std::invalid_argument("Pose output must have shape [1,56,N] or [1,N,56]");
      }

      inline float poseOutputValue(const float *data,
                                   const PoseOutputShape &shape,
                                   int channel,
                                   int prediction)
      {
          if (!data || channel < 0 || channel >= shape.channels ||
              prediction < 0 || prediction >= shape.predictionCount)
          {
              throw std::out_of_range("Pose output channel or prediction is out of range");
          }
          const std::size_t index = shape.layout == PoseOutputLayout::ChannelsFirst
              ? static_cast<std::size_t>(channel) * static_cast<std::size_t>(shape.predictionCount) +
                    static_cast<std::size_t>(prediction)
              : static_cast<std::size_t>(prediction) * static_cast<std::size_t>(shape.channels) +
                    static_cast<std::size_t>(channel);
          return data[index];
      }
  }
  ```

  头文件必须包含 `<cstddef>`, `<cstdint>`, `<stdexcept>` 和 `<vector>`，并使用 `int64_t` 检查 ONNX shape；禁止按 `output0` 或其他节点名分支。

- [ ] **Step 4: Connect the adapter to `PoseOnnxRuntimeEngine`**

  在 `AlgorithmOnYoloPose.h` 引入 `PoseOutputLayout.h`，为引擎增加 `PoseOutputLayout mOutputLayout` 私有字段。构造函数保留 `GetInputNameAllocated(0, allocator)` 和 `GetOutputNameAllocated(0, allocator)`，输入继续严格检查 `[1,3,H,W]`；输出改为调用 `inspectPoseOutputShape(mOutputDims)`，保存布局和预测数量，并把日志写成实际布局和值。

  在 `runInference` 中把运行时 shape 同样传给 `inspectPoseOutputShape`，只有 batch、布局、通道数和预测数与构造时一致才继续；用 `poseOutputValue(output, runtimeShape, channel, prediction)` 替代当前固定的 `channel * mPredictionCount + prediction` lambda。框值、置信度、17 组 `(x,y,confidence)`、NMS、letterbox 逆变换和 `source_algorithm` 均保持现有语义。错误 shape 只记录日志并返回 `false`，不读取越界内存。

- [ ] **Step 5: Register the test target and update the contract documents**

  在 `SVA-server/CMakeLists.txt` 的 `if(BUILD_TESTING)` 中加入：

  ```cmake
  add_executable(PoseOutputLayoutUnitTest
          Analyzer/Tools/PoseOutputLayoutUnitTest.cpp)
  target_include_directories(PoseOutputLayoutUnitTest PRIVATE Analyzer/Core)
  add_test(NAME PoseOutputLayoutUnitTest COMMAND PoseOutputLayoutUnitTest)
  ```

  文档同步三项事实：输入是 OpenCV BGR，经 `blobFromImage(..., swapRB=true, 1/255)` 变成 RGB/NCHW float32；输出允许 `[1,56,N]` 和 `[1,N,56]`，节点名和具体维度由运行时模型读取；当前主算法代码为 `on_yolo11n_pose`，`sleep_yolopose` 与 `on_sleep_yolopose` 只是兼容别名，默认模型文件名为 `yolo11n-pose.onnx`。`BASELINE_RESULTS.md` 中的 `output0`、`[1,56,8400]` 只能标作该模型的实测值，不能写成所有权重的硬编码约束。

- [ ] **Step 6: Run the focused C++ test and the existing regression tests**

  ```bash
  cmake -S SVA-server -B SVA-server/build -DBUILD_TESTING=ON -DSVA_BUILD_POSE_SMOKE_TEST=ON -DSVA_ONNXRUNTIME_GPU=OFF
  cmake --build SVA-server/build --target PoseOutputLayoutUnitTest SleepPoseUnitTest StreamUrlUnitTest
  ctest --test-dir SVA-server/build -R "PoseOutputLayoutUnitTest|SleepPoseUnitTest|StreamUrlUnitTest" --output-on-failure
  ```

  预期三项 CTest 全部通过；真实 ONNX 权重不存在时不执行 `PoseSmokeTest` 视频冒烟，也不把其缺失报告为布局单元测试失败。

- [ ] **Step 7: Commit the task-two checkpoint**

  ```bash
  git add SVA-server/Analyzer/Core/PoseOutputLayout.h SVA-server/Analyzer/Tools/PoseOutputLayoutUnitTest.cpp SVA-server/Analyzer/Core/AlgorithmOnYoloPose.h SVA-server/Analyzer/Core/AlgorithmOnYoloPose.cpp SVA-server/CMakeLists.txt SVA-server/docs/TASK2_IMPLEMENTATION.md SVA-server/tools/sleep_pose_prototype/docs/IO_CONTRACT.md SVA-server/tools/sleep_pose_prototype/docs/BASELINE_RESULTS.md
  git diff --cached --check
  git commit -m "feat: support both pose output layouts"
  ```

  提交前确认 `git diff --cached --name-only` 只包含上述任务二文件。

### Task 2: 任务四 GB28181 配置、同步和预览边界

**Files:**

- Modify: `SVA-backend/ruoyi-admin/src/main/resources/application.yml`
- Modify: `SVA-backend/ruoyi-admin/src/main/java/com/ruoyi/waring/service/impl/HDeviceServiceImpl.java`
- Modify: `SVA-backend/ruoyi-admin/pom.xml`
- Create: `SVA-backend/ruoyi-admin/src/test/java/com/ruoyi/waring/service/impl/HDeviceServiceImplTest.java`

**Interfaces:**

- Consumes: `ZlmServer.host`, `ZlmServer.media_http_port`、现有 `/gb28181/api/devices` 和 `/gb28181/api/sessions` 响应、现有 `HDeviceMapper` 幂等接口。
- Produces: `syncGbDevices()` 继续返回处理数量；同步请求使用配置的 `gb28181.api-port` 和 `gb28181.platform-id`；`previewMonitor(apeId)` 对 GB28181 只返回已保存的 `play_url` 或空字符串。

- [ ] **Step 1: Write the failing service boundary tests**

  在 `HDeviceServiceImplTest` 使用 JUnit 5、Mockito 和 `ReflectionTestUtils` 注入 `HDeviceMapper`、`ZlmServerMapper`、`RestTemplate` 以及两个配置字段。测试固定使用 `ZlmServer.host = "10.0.0.8"`、`media_http_port = 9992`、`gbApiPort = 19080`、`gbPlatformId = "34020000002000000001-test"`，避免依赖真实数据库或 GB 服务。

  覆盖以下四个具体行为：

  ```java
  @Test
  void syncUsesConfiguredEndpointAndPlatformId() {
      when(zlmServerMapper.selectEnabledById(1L)).thenReturn(zlmServer(1L, "10.0.0.8", 9992));
      when(restTemplate.getForEntity(
          "http://10.0.0.8:19080/gb28181/api/devices", String.class))
          .thenReturn(ResponseEntity.ok("{\"code\":0,\"data\":[{\"device_id\":\"34020000001320000001\",\"user_agent\":\"cam-1\",\"online\":true}]}"));
      when(restTemplate.getForEntity(
          "http://10.0.0.8:19080/gb28181/api/sessions", String.class))
          .thenReturn(ResponseEntity.ok("{\"code\":0,\"data\":[{\"device_id\":\"34020000001320000001\",\"state\":\"streaming\",\"stream_id\":\"stream-1\"}]}"));
      when(hDeviceMapper.selectByGbDeviceId("34020000001320000001")).thenReturn(null);

      assertEquals(1, service.syncGbDevices());
      verify(hDeviceMapper).insertDeviceCrud(argThat(device ->
          "gb28181".equals(device.getDevice_type()) &&
          "34020000002000000001-test".equals(device.getGb_platform_id()) &&
          "ws://10.0.0.8:9992/rtp/stream-1.live.flv".equals(device.getPlay_url())));
  }

  @Test
  void invalidPortReportsConfigurationKey() {
      ReflectionTestUtils.setField(service, "gbApiPort", 0);
      when(zlmServerMapper.selectEnabledById(1L)).thenReturn(zlmServer(1L, "10.0.0.8", 9992));

      ServiceException error = assertThrows(ServiceException.class, service::syncGbDevices);

      assertTrue(error.getMessage().contains("gb28181.api-port"));
      verifyNoInteractions(restTemplate);
  }

  @Test
  void blankPlatformIdReportsConfigurationKey() {
      ReflectionTestUtils.setField(service, "gbPlatformId", " ");
      when(zlmServerMapper.selectEnabledById(1L)).thenReturn(zlmServer(1L, "10.0.0.8", 9992));

      ServiceException error = assertThrows(ServiceException.class, service::syncGbDevices);

      assertTrue(error.getMessage().contains("gb28181.platform-id"));
      verifyNoInteractions(restTemplate);
  }

  @Test
  void controlApiFailureDoesNotReconcileExistingDevicesOffline() {
      HDevice existing = new HDevice();
      existing.setIs_online("1");
      when(zlmServerMapper.selectEnabledById(1L)).thenReturn(zlmServer(1L, "10.0.0.8", 9992));
      when(restTemplate.getForEntity(
          "http://10.0.0.8:19080/gb28181/api/devices", String.class))
          .thenThrow(new ResourceAccessException("connection refused"));

      assertEquals(0, service.syncGbDevices());
      verify(hDeviceMapper, never()).updateGbDeviceOffline(anyList());
      verify(hDeviceMapper, never()).updateDevice(existing);
  }
  ```

  另加 `previewMonitor` 的两个测试：GB28181 `play_url` 为空时断言返回 map 的 `playUrl` 为空且 `zlmServerMapper` 不用于构造 RTSP 地址；普通 RTSP DIRECT 夹具设置 `ape_id=cam-1`、ZLM app 为 `live` 且 `media_http_port=9992` 时断言返回 `ws://10.0.0.8:9992/live/cam-1.live.flv`。测试夹具不调用真实 HTTP、MySQL、Redis 或 ZLMediaKit。

- [ ] **Step 2: Run the focused Java tests before implementation and verify they fail**

  ```bash
  mvn -pl SVA-backend/ruoyi-admin -am -Dtest=HDeviceServiceImplTest -DfailIfNoTests=false test
  ```

  预期当前仓库没有该测试类和配置字段，测试目标不能完成；记录失败原因后继续实现，不修改数据库数据。

- [ ] **Step 3: Add exact GB28181 configuration**

  在 `SVA-backend/ruoyi-admin/src/main/resources/application.yml` 的基础应用配置中加入：

  ```yaml
  gb28181:
    api-port: 18080
    platform-id: 34020000002000000001
  ```

  在 `HDeviceServiceImpl` 增加 `@Value("${gb28181.api-port:0}") private Integer gbApiPort;` 和 `@Value("${gb28181.platform-id:}") private String gbPlatformId;`。默认值只用于让缺失配置进入显式校验，不能写回 `18080` 或平台 ID 常量。

- [ ] **Step 4: Replace synchronization constants with validated configuration**

  在 `fetchGbDevicesFromZlm` 开始处校验 ZLM host、`gbApiPort` 范围 `1..65535` 和非空 `gbPlatformId`；无效时抛出 `ServiceException`，异常消息分别包含精确键名 `gb28181.api-port` 或 `gb28181.platform-id` 并记录同一键名。构造 API base URL 时只使用：

  ```java
  String base = "http://" + zlmServer.getHost() + ":" + gbApiPort + "/gb28181/api";
  ```

  `dto.setPlatformId(gbPlatformId)` 替代源码字面量。保留 `/devices` 的 `code=0`、`data` 数组检查；`/sessions` 仅采纳 `state=streaming` 且同时有 `device_id`、`stream_id` 的记录。任何控制 API 连接异常、空响应、非零 code 或非数组响应都返回 `null`，由 `syncGbDevices` 保留已有状态并跳过 `updateGbDeviceOffline`；只有完整成功的设备快照才做离线收敛。

  生成 GB `play_url` 时只在 `zlmServer.getMedia_http_port()` 非空且为正数时，按 `ws://10.0.0.8:9992/rtp/stream-1.live.flv` 的格式使用实际 host、端口和会话 `stream_id` 拼接；删除当前 `9992` 默认值。同步仍按 `gb_device_id` 查找并更新，不新增数据库列，也不改变重复同步的幂等处理。

- [ ] **Step 5: Make preview protocol-aware**

  在 `previewMonitor` 中删除未使用的 `previewAddProxyUrl` 和 `direct_source_url` 播放回退。先读取并规范化 `device.getPlay_url()`；当 `device_type` 为 `gb28181` 时，结果中的 `playUrl` 只能是该值或空字符串；当设备为普通 RTSP DIRECT 且 `play_url` 为空时，才调用现有 `buildDirectPlayUrl(device)`。结果继续返回 `apeId`、`name`、`streamSourceType`、`monitorStatus`、`directSourceUrl`、`playUrl`、IP/端口和状态枚举，并增加 `deviceType`、`isOnline`、`gbDeviceId`、`gbPlatformId` 以便前端精确展示。

- [ ] **Step 6: Add the test dependency and run the service tests**

  在 `ruoyi-admin/pom.xml` 的 `<dependencies>` 中加入 `spring-boot-starter-test`，并设置 `<scope>test</scope>`；不改生产依赖版本。然后执行：

  ```bash
  mvn -pl SVA-backend/ruoyi-admin -am -Dtest=HDeviceServiceImplTest test
  mvn -pl SVA-backend/ruoyi-admin -am -DskipTests compile
  ```

  预期服务边界测试全部通过，且编译不再出现硬编码配置引用。若本机 Maven 无法下载依赖，只记录依赖下载阻塞，不用源码静态检查替代测试通过。

- [ ] **Step 7: Commit the task-four checkpoint**

  ```bash
  git add SVA-backend/ruoyi-admin/src/main/resources/application.yml SVA-backend/ruoyi-admin/src/main/java/com/ruoyi/waring/service/impl/HDeviceServiceImpl.java SVA-backend/ruoyi-admin/pom.xml SVA-backend/ruoyi-admin/src/test/java/com/ruoyi/waring/service/impl/HDeviceServiceImplTest.java
  git diff --cached --check
  git commit -m "fix: configure gb28181 synchronization"
  ```

  提交前执行 `rg -n "final int gbApiPort|34020000002000000001|:18080" SVA-backend/ruoyi-admin/src/main/java/com/ruoyi/waring/service/impl/HDeviceServiceImpl.java`，不得再在该 Java 源文件中匹配同步常量。

### Task 3: 任务五设备 API、公共播放规则和设备/布控页面

**Files:**

- Modify: `SVA-web/src/api/device.js`
- Create: `SVA-web/src/utils/mediaPlayback.js`
- Modify: `SVA-web/src/views/device/manage.vue`
- Modify: `SVA-web/src/views/device/realtime.vue`
- Modify: `SVA-web/src/views/deployment/add.vue`
- Modify: `SVA-web/src/components/RTSPPlayer/index.vue`

**Interfaces:**

- Consumes: `/waring/device/monitor/{apeId}/preview` 的 `data.playUrl`、设备列表中的 `device_type`/`stream_source_type`/`is_online`/`monitor_status`。
- Produces: `syncGbDevices()` 的 POST 调用；`extractPlayableUrl(source)` 返回合法浏览器地址或空字符串；所有播放器接收的 `url` 都经过 `isBrowserPlayableUrl`。

- [ ] **Step 1: Write the failing static contract checks**

  从 `SVA-web` 目录执行以下检查，当前代码应能找到旧导入、旧请求和旧示例回退：

  ```bash
  rg -n "setUrl|getTest|setTest|http://127\\.0\\.0\\.1:8080/video|live/acceptance|sourceType: ['\"]device['\"]" src
  ```

  预期命中 `views/shipin/index.vue`、播放器/实时页面和大屏回退；这些命中在实现后必须归零。该静态检查不扫描 SQL 验收夹具。

- [ ] **Step 2: Implement the pure media playback module**

  新建 `SVA-web/src/utils/mediaPlayback.js`，只做字段提取和协议判断，不拼接 host、port、app、stream：

  ```js
  const PLAY_URL_FIELDS = [
    'playUrl', 'play_url', 'previewUrl', 'preview_url',
    'url', 'streamUrl', 'stream_url', 'flvUrl', 'flv_url'
  ]

  export function isBrowserPlayableUrl(url) {
    if (typeof url !== 'string') return false
    const value = url.trim()
    return /^(https?:\\/\\/|wss?:\\/\\/|\\/)/i.test(value)
  }

  export function extractPlayableUrl(source) {
    const data = source && source.data && typeof source.data === 'object'
      ? source.data
      : source
    if (typeof data === 'string') {
      return isBrowserPlayableUrl(data) ? data.trim() : ''
    }
    if (!data || typeof data !== 'object') return ''
    for (let index = 0; index < PLAY_URL_FIELDS.length; index += 1) {
      const value = data[PLAY_URL_FIELDS[index]]
      if (isBrowserPlayableUrl(value)) return value.trim()
    }
    return ''
  }

  export function isFlvUrl(url) {
    return typeof url === 'string' && /\\.flv(?:[?#]|$)/i.test(url.trim())
  }
  ```

  `rtsp://`、`gb://`、`gb28181://` 不匹配允许协议，因此不能进入 HTML video 或 flv.js；`directSourceUrl`/`direct_source_url` 不列为播放候选。

- [ ] **Step 3: Add the GB sync API and correct device management fields**

  在 `device.js` 增加：

  ```js
  export function syncGbDevices() {
    return request({
      url: '/waring/device/gb28181/sync',
      method: 'post'
    })
  }
  ```

  在 `manage.vue` 导入该函数并新增同步按钮；成功时读取后端消息、调用 `getList()`，失败时显示错误且不清空当前列表。列表拆成四个独立展示：`formatDeviceType(row.device_type)` 返回 `RTSP`/`GB28181`，`formatSourceType(row.stream_source_type)` 返回 `直连`/`平台`，`formatOnline(row.is_online)` 只把字符串 `1` 显示为 `在线`，其他值显示 `离线`，`formatMonitorStatus(row.monitor_status)` 显示 `RUNNING`/`STOPPED`/`STARTING`/`STOPPING`/`ERROR` 对应的中文状态。把现有把 `stream_source_type` 标成“设备类型”的列和表单标签改成“流来源”。

  `handlePreview` 只执行 `previewDeviceMonitor(apeId)` 后的 `extractPlayableUrl(response)`；无地址显示“暂无可用播放流”，请求异常只显示错误，不回退到 `row.play_url` 或 `row.direct_source_url`。普通新增设备的 `device_type` 默认写入 `rtsp`，同步得到的 GB 设备保持后端 `gb28181` 值。

- [ ] **Step 4: Remove address guessing from realtime/deployment/player components**

  在 `realtime.vue` 删除 `normalizeStreamUrl` 和本地 `extractPreviewUrl`，`playDeviceInActiveSlot` 只使用 `extractPlayableUrl(await previewDeviceMonitor(apeId))`；没有地址设置卡片错误，不读取设备行的原始 URI。`initSlotPlayer` 用 `isFlvUrl` 和 `isBrowserPlayableUrl` 判断，FLV 交给 flv.js，其他允许协议交给原生 video，非法协议直接显示失败并清理 src。`handleAddToWall` 也必须先通过 `extractPlayableUrl`，保存 `sourceType: 'realtime'`。同时把 `handleStart`/`handleStop` 改为读取响应 `data.success` 与 `data.shortMessage` 后再提示并刷新列表，不通过按钮点击结果猜测监控状态。

  在 `deployment/add.vue` 的 `handleDeviceChange` 删除 `opt.raw.play_url`/`opt.raw.direct_source_url` 两级回退和异常分支中的 fallback；预览异常时清空 `streamUrl`、销毁播放器并提示“获取实时流地址失败”。在调用 `playStream` 前用 `isBrowserPlayableUrl` 过滤，确保布控页面不会把 RTSP/GB URI交给播放器。

  在 `RTSPPlayer/index.vue` 删除 `isRtspUrl`、`isGbUrl` 和从当前 hostname/9992/app/stream 构造地址的代码；`initFLVPlayer` 先运行 `extractPlayableUrl(this.rtspUrl)`，空地址销毁当前播放器，`.flv` 地址用现有 flv.js 生命周期，其他合法浏览器地址用现有 `playHttpMedia`，非法地址不播放。保留重连、销毁、画布/视频元素清理逻辑。

- [ ] **Step 5: Remove the legacy test-page request**

  在 `shipin/index.vue` 删除 `getAlarmPhoto`、`setUrl`、`getTest`、`setTest`、`store` 和 `axios` 导入；删除 `fetchData`、`handleClick`、`inputId`、`deviceImages` 及模板中对应的输入框和调用按钮。保留 `img1` 静态图片容器与 `algorithm` 状态展示，使页面不再产生未定义的 API 请求。

- [ ] **Step 6: Run frontend lint/build and repeat the static checks**

  ```bash
  cd SVA-web
  npm run lint -- --no-fix
  npm run build:prod
  if rg -n "setUrl|getTest|setTest|http://127\\.0\\.0\\.1:8080/video|live/acceptance|sourceType: ['\"]device['\"]" src; then exit 1; fi
  ```

  预期 ESLint 和生产构建退出码均为 `0`；旧 API、示例播放地址和非法 `sourceType` 在 `SVA-web/src` 中无匹配。若依赖目录不存在，先按仓库锁文件安装依赖并把安装/网络问题单独记录，不修改锁文件版本。

- [ ] **Step 7: Commit the device/frontend checkpoint**

  ```bash
  git add SVA-web/src/api/device.js SVA-web/src/utils/mediaPlayback.js SVA-web/src/views/device/manage.vue SVA-web/src/views/device/realtime.vue SVA-web/src/views/deployment/add.vue SVA-web/src/components/RTSPPlayer/index.vue SVA-web/src/views/shipin/index.vue
  git diff --cached --check
  git commit -m "feat: enforce browser playback contract"
  ```

### Task 4: 任务五监控墙、告警筛选和 SQL 验收夹具

**Files:**

- Modify: `SVA-web/src/views/dping/components/center-switch-panel.vue`
- Modify: `SVA-web/src/views/dping/components/right-monitor-panel.vue`
- Modify: `SVA-web/src/api/screenWall.js`
- Modify: `SVA-web/src/views/warning/index.vue`
- Modify: `SVA-backend/ruoyi-admin/src/main/resources/mapper/waring/HWaringMapper.xml`
- Modify: `SVA-backend/docs/sql/create_h_screen_wall_stream.sql`

**Interfaces:**

- Consumes: `/screen-wall/streams` 的 `source_type`、`source_id`、`device_id`、`play_url`；设备预览接口；`HWaringMapper.xml` 的 `alarm_type` 参数。
- Produces: 监控墙只产生 `sourceType=task|realtime`；空监控墙临时卡片逐个通过预览接口取地址；睡岗查询发送 `alarm_type=SVA_SLEEP`。

- [ ] **Step 1: Write the failing source-type and alarm-field checks**

  从仓库根目录执行：

  ```bash
  rg -n "sourceType: ['\"]device['\"]|pickValue\(source, \['sourceType', 'source_type'\]" SVA-web/src/views/dping SVA-web/src/api/screenWall.js
  rg -n "alarm_type = #\\{alarm_type\\}" SVA-backend/ruoyi-admin/src/main/resources/mapper/waring/HWaringMapper.xml
  ```

  预期第一条命中大屏空列表回退，第二条当前只命中管理员查询路径；实现后前者归零，后者在 `selectWaringList` 和 `selectWaringListByOrgIndex` 两条路径都命中。

- [ ] **Step 2: Constrain screen-wall API payloads**

  在 `screenWall.js` 增加精确的 `normalizeSourceType(value)`：只返回小写 `realtime` 或 `task`，其他值返回空字符串。`buildScreenWallUpsertPayload` 使用该函数；`upsertScreenWallStream` 在 payload 的 `sourceType` 为空时返回拒绝的 Promise 并显示“监控墙源类型只支持 realtime 或 task”的错误，不向后端发送 `device`。`normalizeScreenWallStream` 保留后端返回值但把非法类型归为空，调用方过滤空类型。

- [ ] **Step 3: Fix the center monitor-wall fallback and playback**

  在 `center-switch-panel.vue` 导入 `previewDeviceMonitor`、`extractPlayableUrl` 和 `isBrowserPlayableUrl`。当 `/screen-wall/streams` 没有可用记录时，读取设备列表后对每台设备调用 `previewDeviceMonitor(d.ape_id)`，只为得到合法 `playUrl` 的设备构造：

  ```js
  {
    id: d.ape_id,
    sourceId: d.ape_id,
    sourceType: 'realtime',
    deviceId: d.ape_id,
    name: d.name || d.ape_id,
    slotIndex: index,
    playUrl
  }
  ```

  删除当前基于 `direct_source_url`/`play_url` 的本地拼接。已保存的流只保留 `sourceType` 为 `task` 或 `realtime` 且 `playUrl` 通过 `isBrowserPlayableUrl` 的记录；任务流继续按 `taskPushEnabled` 选择算法流或设备预览地址，但算法流无效时保留原有效地址。`playStream` 在播放前再次检查，非法地址将卡片设为 `failed` 并释放播放器。

- [ ] **Step 4: Fix the right monitor panel**

  在 `right-monitor-panel.vue` 复用 `extractPlayableUrl` 和 `isBrowserPlayableUrl`；`openRealtimeStreams` 的预览结果必须经过公共提取函数，`playStream` 只有在地址合法时才能设置 `video.src` 或创建 flv.js。没有地址时保持 `failed`，不从设备列表补用 `direct_source_url` 或原始 RTSP/GB 地址。保留现有定时刷新和播放器销毁。

- [ ] **Step 5: Add the exact sleep-alarm filter**

  在 `warning/index.vue` 的查询表单增加一个“睡岗告警”选择项，选项文本为“睡岗告警”，值固定为 `SVA_SLEEP`，绑定 `querySpecificParams.alarm_type`；保留原有 `alarm_type_name` 下拉和其选项加载。初始化、路由参数清理和 `activated` 重置都加入 `alarm_type: undefined`，`fetchData` 继续把 `querySpecificParams` 合并进 `getWarningList`，因此浏览器请求会出现 `alarm_type=SVA_SLEEP` 而不是显示文本。

  在 `HWaringMapper.xml` 的 `selectWaringListByOrgIndex` `<where>` 中，在 `alarm_type_name` 条件前加入与管理员查询相同的：

  ```xml
  <if test="alarm_type != null and alarm_type != ''">
      AND alarm_type = #{alarm_type}
  </if>
  ```

  不改变现有 `alarm_type_name` 的兼容筛选，也不改睡岗入库值。

- [ ] **Step 6: Correct the SQL acceptance source type**

  在 `create_h_screen_wall_stream.sql` 把表注释的源类型改为 `task/realtime`，把验收流插入中的 `source_type` 从 `'device'` 改为 `'realtime'`。保留显式 SQL 夹具中的 `rtsp://127.0.0.1:9994/live/acceptance` 和 `http://127.0.0.1:9992/live/acceptance.live.flv`，因为它们不是播放器运行时默认值；前端源代码中不得重新出现这些地址。

- [ ] **Step 7: Run frontend/backend static and build verification**

  ```bash
  cd SVA-web
  npm run lint -- --no-fix
  npm run build:prod
  cd ..
  mvn -pl SVA-backend/ruoyi-admin -am -DskipTests package
  if rg -n "sourceType: ['\"]device['\"]|http://127\\.0\\.0\\.1:8080/video|live/acceptance" SVA-web/src; then exit 1; fi
  if rg -n "sourceType.*device|source_type.*device" SVA-backend/docs/sql/create_h_screen_wall_stream.sql; then exit 1; fi
  rg -n "alarm_type=SVA_SLEEP|alarm_type = #\\{alarm_type\\}" SVA-web/src/views/warning/index.vue SVA-backend/ruoyi-admin/src/main/resources/mapper/waring/HWaringMapper.xml
  ```

  预期 Vue 构建和 Maven 打包退出码为 `0`；前端运行时无旧视频 API、示例回退或非法源类型；SQL 不再写入 `device`；mapper 两条列表查询都保留精确 `alarm_type` 条件。真实数据库迁移和浏览器网络记录另列为外部联调证据，不由构建成功替代。

- [ ] **Step 8: Commit the task-five checkpoint**

  ```bash
  git add SVA-web/src/views/dping/components/center-switch-panel.vue SVA-web/src/views/dping/components/right-monitor-panel.vue SVA-web/src/api/screenWall.js SVA-web/src/views/warning/index.vue SVA-backend/ruoyi-admin/src/main/resources/mapper/waring/HWaringMapper.xml SVA-backend/docs/sql/create_h_screen_wall_stream.sql
  git diff --cached --check
  git commit -m "fix: align task5 monitor wall and alarm contracts"
  ```

### Task 5: 整个平台分层验收和证据记录

**Files:**

- Modify only if needed for evidence: `docs/2026-09-07-full-platform-verification-record.md`
- Do not stage: `SVA-mediaServer/gb28181/tests/__pycache__/`, `SVA-mediaServer/gb28181/tools/__pycache__/`

**Interfaces:**

- Consumes: 三个阶段提交后的源码、CTest/JUnit/Maven/Vue 构建结果以及可用的 WSL 运行环境。
- Produces: 只记录已实际执行的静态、构建和接口证据；真实 ONNX 权重、数据库、Redis、ZLMediaKit、GB 摄像机和浏览器媒体播放条件缺失时明确记录阻塞。

- [ ] **Step 1: Verify repository state and contracts**

  ```bash
  git status --short --branch
  git log --oneline -4
  rg -n "18080|34020000002000000001|sourceType: ['\"]device['\"]|live/acceptance|127\\.0\\.0\\.1:8080/video|setUrl|getTest|setTest" SVA-backend/ruoyi-admin/src/main/java SVA-web/src
  ```

  预期 GB 两个值只在 `application.yml`/测试夹具中出现，前端运行时代码没有旧回退；若仍有命中，先定位属于配置、测试、文档还是运行时代码，再决定是否修正，不能按字符串相似性批量删除。

- [ ] **Step 2: Run all available automated checks**

  ```bash
  cmake --build SVA-server/build --target PoseOutputLayoutUnitTest SleepPoseUnitTest StreamUrlUnitTest
  ctest --test-dir SVA-server/build --output-on-failure
  mvn -pl SVA-backend/ruoyi-admin -am test
  cd SVA-web
  npm run lint -- --no-fix
  npm run build:prod
  ```

  每条命令记录退出码和关键输出；没有真实权重时不执行或不宣称 `PoseSmokeTest` 成功。

- [ ] **Step 3: Run only available local service checks**

  如果本机已有 MySQL/Redis/ZLMediaKit/GB 控制 API，按现有部署脚本启动后检查：

  ```bash
  sync_response=$(curl -sS -X POST http://127.0.0.1:9114/waring/device/gb28181/sync)
  device_response=$(curl -sS http://127.0.0.1:9114/waring/device/list)
  printf '%s\n%s\n' "$sync_response" "$device_response"
  ```

  只在 `device_response` 返回实际 `ape_id` 后，用该原样值请求对应的 `/waring/device/monitor/{apeId}/preview`；不得凭名称猜测设备编码。检查同步 URL/平台配置、幂等数量、GB 无 `play_url` 时的空地址和 RTSP 设备的代理地址；如果外部服务不可用，记录精确连接错误。

- [ ] **Step 4: Record evidence without fabricating external validation**

  在已有 `docs/2026-09-07-full-platform-verification-record.md` 中只补充实际命令、时间、分支、提交 SHA、输出摘要和外部前置条件。把“代码/构建通过”“接口在模拟响应下通过”“真实媒体播放通过”分成三类，禁止用 SQL 验收夹具或静态检查代替真实摄像机/浏览器结论。

- [ ] **Step 5: Perform final diff review and commit only evidence changes**

  ```bash
  git diff --check HEAD~3..HEAD
  git status --short
  ```

  若验收记录有修改，只提交该记录文件；不提交 `__pycache__`，不删除用户现有未跟踪内容，不推送远端。

## Handoff

Plan complete and saved to `docs/superpowers/plans/2026-09-07-task5-implementation-plan.md`. Two execution options:

1. **Subagent-Driven (recommended)** — dispatch a fresh subagent per task and review each checkpoint before continuing.
2. **Inline Execution** — execute the tasks in this session using `superpowers:executing-plans`, with checkpoints after task two, task four, and task five.

Because the requested scope is the local `task-5` branch, inline execution keeps all changes in this checkout and is the preferred option unless a separate task review is wanted.
