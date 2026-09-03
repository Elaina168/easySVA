# 布控算法流兼容性修复设计

## 背景

当前前端在打开布控视频或加入监控墙时，会调用 `POST /deployments/{id}/live-output` 动态启用算法画框流。现有部署使用的后端 `master` 版本没有该接口，现有 Analyzer 也没有 `/api/control/live-output` 路由，因此请求被 Spring 当作静态资源请求并返回 `No static resource`。

现场检查同时确认：原始 RTSP 输入流在线，布控任务保存了有效的 `algorithmStreamUrl` 且 `pushEnabled=true`；算法输出流曾经成功注册，但 MediaServer 重启后旧 Analyzer 留下失效连接，没有重新建立推流。

## 目标与范围

本次修复服务于第一次验收，目标是恢复原有 RTSP 预览、YOLO 布控检测和算法流播放，不扩展动态按需推流协议。

修改范围只包含：

- 前端直接使用布控详情中已有的算法流地址；如果该地址不可用，再回退到设备原始预览地址，不再把动态启用接口作为播放前置条件。
- 恢复运行环境时保证只有一个 Analyzer 实例，并在 MediaServer 已运行后启动 Analyzer。
- 重新启动布控任务，使 Analyzer 重新拉取输入流并向 MediaServer 注册算法输出流。

不修改后端接口，不为 Analyzer 新增 `/api/control/live-output`，不改变数据库结构和已有布控数据。

## 前端数据流

布控页面获取任务详情后，检查任务中的 `pushEnabled` 和 `algorithmStreamUrl`。当任务已启用推流且地址存在时，直接播放该算法流；否则调用设备预览接口获取原始视频地址。当前版本不调用后端和 Analyzer 均未实现的动态启用接口。

该兼容策略应用于三个现有入口：布控列表加入监控墙、布控编辑页预览、监控大屏加载布控流，避免不同页面出现不一致行为。

## 运行恢复顺序

先保持 MediaServer 正常运行，再结束所有重复 Analyzer 进程并启动单个 Analyzer。清除旧运行态后重新启动指定布控任务，让它重新创建 Worker、拉取 `live/cam893884` 并推送 `analyzer/{deploymentId}`。后端保持当前版本运行。

## 验证标准

- 前端构建成功，没有新增 lint 或编译错误。
- 页面播放流程不再请求当前部署未实现的 `POST /deployments/{id}/live-output`。
- WSL 中只有一个 Analyzer 进程监听 9993。
- 原始输入流 `live/cam893884` 在线。
- 算法输出流 `analyzer/controldtFFAaYCDwYSu4` 在 ZLMediaKit 中注册。
- 页面能打开布控视频，不再显示 `No static resource .../live-output`。
- 布控任务保持运行，并能继续产生 YOLO 告警。

## 回退方式

前端改动集中在三个播放地址解析函数，可通过对应提交反向恢复。运行恢复只清理重复进程并重建内存态，不删除数据库记录或媒体文件。
