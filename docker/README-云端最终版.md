# 云端最终版编排源码

此目录保存最终部署包所使用的 Docker Compose、Nginx、初始化 SQL、容器入口和一键生命周期脚本。

为避免在源代码包中重复提供大体积编译产物，本目录不包含 `backend.jar`、前端 `dist`、Analyzer、MediaServer、GbSipServer 可执行文件、模型和演示视频；这些产物包含在单独提交的安装包中。

运行安装包前复制 `env.example` 为 `.env` 并设置随机内部口令。视觉模型 API Key 只能在部署环境中配置，不应写入源码或示例配置。

一键脚本：`start.sh` 构建并启动，`stop.sh` 安全停止且保留数据，`status.sh` 查看状态，`publish.sh` 在本机验收后开放 Web 监听。完整运行请使用比赛同时提交的最终安装包。
