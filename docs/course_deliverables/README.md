# easySVA 安全视频分析平台 - 课程全套交付文档库

**项目名称**：easySVA (easy Surveillance Video Analytics)  
**适用课程**：大三软件工程实训·企业应用设计实践（Hackathon 敏捷实战）  
**指导教师**：吴军、孙溢  
**版本编号**：v2.5 (全套交付结项版)  
**交付时间**：2026年9月  

---

## 一、 交付文档体系总览

本项目已严格按照高校实训教学要求与企业级工业软件交付规范，编制了 **10 本全景技术与管理文档** 及 **1 套 Docker 一键免编译部署包**：

| 序号 | 文档名称 | 英文主题 | 核心内容概括 | 在线阅读链接 |
| :---: | :--- | :--- | :--- | :---: |
| **01** | **需求规格说明书** | System Requirements Specification | 阐明 GB28181 接入、YOLO-Pose 睡岗检测、云台控制、算法热重载及 Docker 交付规范 | [01_需求文档.md](./01_需求文档.md) |
| **02** | **总体架构设计说明书** | System Architecture Document | 5层总体架构、Docker 7微服务容器拓扑、算法热更新时序与端口规划 | [02_架构设计文档.md](./02_架构设计文档.md) |
| **03** | **详细设计说明书 (算法与国标)**| Algorithm & Protocol Detail Design | YOLO-Pose 几何数学模型、Hot-Reload 并发安全、8 字节 Hex 封包规范 | [03_设计文档_算法与国标.md](./03_设计文档_算法与国标.md) |
| **04** | **接口规范说明书** | API Specification | 设备管理、国标 PTZ、布控告警、算法动态参数调优及 WebSocket 推流接口 | [04_接口文档.md](./04_接口文档.md) |
| **05** | **系统部署文档与运维手册** | Deployment & Operations Guide | **Docker Desktop 一键免编译部署**与 Linux/WSL 原生开发双轨部署指南 | [05_部署文档_含部署手册.md](./05_部署文档_含部署手册.md) |
| **06** | **测试方案与测试报告** | System Test Report | 15 项端到端全链路自动化验收与专项深度测试报告（100% PASS） | [06_测试文档.md](./06_测试文档.md) |
| **07** | **敏捷项目管理与执行文档** | Agile Project Management | 4 大专业组分工、6 个 Sprint 迭代冲刺历程及交付物全景矩阵 | [07_项目管理文档.md](./07_项目管理文档.md) |
| **08** | **用户操作与系统使用手册** | User Manual | 用户图文操作手册，含算法调优弹窗、电脑摄像头实景接入、PTZ控制实操 | [08_使用手册.md](./08_使用手册.md) |
| **09** | **系统全景与核心架构图集** | Diagrams Collection | 汇集 10 张高清 Mermaid 架构图：总体、部署、Docker、数据流、SIP、PTZ等 | [09_架构图.md](./09_架构图.md) |
| **10** | **系统运维与排障脚本手册** | Operations & Runbook | 涵盖全生命周期重启自愈、双国标上下线控制、高可用转推守护及监控看板 | [10_系统运维与排障脚本手册.md](./10_系统运维与排障脚本手册.md) |

---

## 二、 核心交付特快通道：Docker 一键免编译部署包

针对评审专家、答辩老师及组员电脑，系统制作了完全解耦的 Docker 容器化一键部署包：
- **部署包路径**：`C:\Users\34964\Desktop\easySVA-部署包`
- **极速 3 步启动**（在 PowerShell 执行）：
  ```powershell
  cd "C:\Users\34964\Desktop\easySVA-部署包"
  docker load -i easysva-images-v1.0.tar
  docker compose up -d
  # 浏览器直接打开 http://localhost (admin / admin123)
  ```
- **技术特性**：
  - 零开发环境依赖（免装 JDK/MySQL/Redis/OpenCV/FFmpeg）；
  - 编排 7 大微服务容器（`sva-mysql`, `sva-redis`, `sva-media`, `sva-analyzer`, `sva-backend`, `sva-web`, `sva-simulator`）；
  - 自适应硬件回退（有显卡启用 GPU 加速，无显卡自动回退 CPU 软编）；
  - 数据卷持久化（`mysql-data`, `redis-data`, `sva-upload`）。

---

## 三、 特色功能与技术亮点矩阵

1. **工业级国标 GB/T 28181-2016 深度接入**：支持 SIP 注册、Digest 鉴权、动态心跳保活、INVITE 点播与 PS-RTP 解复用。
2. **YOLO-Pose 单阶段多人体姿态估计与睡岗识别**：17 关键点几何空间夹角解算（$	heta_{torso}$, $	heta_{head}$, $	heta_{knee}$）结合多帧时序滑动窗口消抖。
3. **算法超参数在线动态热重载 (Hot-Reload)**：C++ 调度器与推理线程池读写锁保护，前端滑块实时微调，毫秒级生效，0 丢帧 0 重启。
4. **工位电脑摄像头实景注入**：浏览器 WebSocket (`:18090`) 转 RTMP 并虚拟为独立国标设备，支持演练人员实景模拟睡岗触发告警。
5. **国标标准八向云台控制 (PTZ)**：8 字节十六进制指令封装（首字节 `0xA5`、速度控制、Checksum 取模校验），前端八向虚拟雷盘联动。
6. **全链路自愈运维保障体系**：双国标转桥守护进程（`gb_bridge_daemon.py`）在线状态感知，杜绝 404/409 盲目推流；一键启停脚本（`restart.sh`, `device_online.sh`, `device_offline.sh`）。
