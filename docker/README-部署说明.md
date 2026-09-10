# easySVA 一键部署说明（组员版 · 免编译）

本包把已经在 WSL 上编译、调试通过的 easySVA 全部服务做成了 Docker 镜像，
**你不需要编译任何源码、不需要装 JDK/MySQL/Redis/ffmpeg**，只要有 Docker Desktop 即可跑起来。

---

## 一、你需要准备什么

1. **Docker Desktop**（Windows）
   - 没装：到 https://www.docker.com/products/docker-desktop/ 下载安装，安装时勾选使用 WSL 2。
   - 装完**启动 Docker Desktop**，等左下角鲸鱼图标变成绿色（Engine running）。
   - 验证：打开 PowerShell 输入 `docker version`，能打印 Client 和 Server 两段即正常。
2. **本机 80 端口要空闲**（很重要，见“常见问题 1”）。
3. 内存建议 Docker 分配 ≥ 6GB（Docker Desktop → Settings → Resources）。

---

## 二、包里有什么

```
easySVA-组员部署包/
├─ easysva-images-v1.0.tar     # 7 个服务的镜像（需先 load，见下）
├─ docker-compose.yml          # 一键编排文件
├─ .env                        # 端口/密码等参数（一般不用改）
└─ build/mysql-init/           # 数据库初始化脚本（首次启动自动导入，别动）
   ├─ 01-schema.sql
   └─ 02-seed-fix.sql
```

7 个容器分别是：

| 容器 | 作用 |
|---|---|
| sva-mysql | MariaDB 数据库 |
| sva-redis | Redis 缓存 |
| sva-media | ZLMediaKit 流媒体 + GB28181 国标信令 + 转桥 |
| sva-analyzer | C++ 算法引擎（YOLO 目标检测 / 姿态睡岗检测） |
| sva-backend | Java（若依）后端 |
| sva-web | nginx 前端 + 接口反代 + 抓拍/录像静态服务 |
| sva-simulator | 国标模拟摄像头（自带瞌睡视频，开机自动注册推流） |

---

## 三、三步启动

> 以下命令在 **PowerShell** 里执行，先 `cd` 到本部署包目录（docker-compose.yml 所在目录）。

### 第 1 步：导入镜像（只需做一次）

```powershell
docker load -i easysva-images-v1.0.tar
```

看到多行 `Loaded image: easysva-xxx:1.0`、`Loaded image: mariadb:10.6` 等即成功。
用 `docker images` 应能看到 5 个 easysva-* 和 mariadb、redis 共 7 个镜像。

### 第 2 步：一键启动全部服务

```powershell
docker compose up -d
```

第一次启动会自动建数据库、导入初始数据、自动注册模拟摄像头，**约 30~60 秒**全部就绪。
查看状态（等到都是 `Up`，其中 mysql/redis/media 显示 `healthy`）：

```powershell
docker compose ps
```

### 第 3 步：打开网页

浏览器访问： **http://localhost** （注意是 80 端口，直接 localhost，不要加 8080）

- 用户名：`admin`
- 密码：`admin123`
- 已关闭图形验证码，直接登录即可。

---

## 四、验收演示流程（睡岗检测 + 国标接入）

1. 登录后进入 **布控管理 → 布控列表**，能看到一条示例布控 `gbsleep001 睡岗检测-国标设备`，初始状态 `STOPPED`。
2. 点该行 **启动**，提示“布控已启动”、状态变 `RUNNING`。
3. 进入 **设备管理 → 实时监控（工作台）**，把设备加入监控墙 / 单分屏，可看到模拟摄像头推送的瞌睡人物视频，画面带算法分析。
4. 持续几秒满足“低头睡岗”规则后：
   - 右下角 **报警推送** 弹窗，显示**睡岗告警 + 现场抓拍图**；
   - **报警管理 → 报警列表**出现睡岗告警记录，点看详情可见**抓拍图和告警录像**；
   - 大屏（顶部“大屏”）同步刷新告警统计。
5. 演示完点该行 **停止** 即可。

> 算法在没有 NVIDIA 显卡的电脑上会**自动回退到 CPU 软编码（libx264）**，日志里出现
> `NVENC open failed, falling back to software H.264` 是**正常现象**，不影响出图和告警。

---

## 五、常用命令（都在部署包目录执行）

```powershell
docker compose ps                 # 看各服务状态
docker compose logs -f sva-analyzer   # 跟踪算法引擎日志（Ctrl+C 退出跟踪，不影响服务）
docker compose logs -f sva-backend    # 跟踪后端日志
docker compose restart sva-backend    # 只重启某个服务
docker compose stop               # 停止全部（数据保留）
docker compose start              # 再次启动
docker compose down               # 停止并删除容器（数据保留，再 up 还在）
docker compose down -v            # 【彻底重置】连数据库/抓拍一起清空，回到首次干净状态
```

### 不想要自带的模拟摄像头？

```powershell
docker compose stop simulator     # 关掉模拟摄像头，其余服务照常
```

### 接自己的真实 GB28181 国标摄像头

在平台“设备管理”里新增国标设备，摄像头/SIP 端按以下参数注册到本机：
- SIP 服务器 IP：本机 IP；SIP 端口：**5060**（TCP/UDP）
- 平台编号（server id）：`34020000002000000001`
- 域（realm）：`3402000000`
- 密码：`12345678`
- RTP 收流端口段：30002–30100（UDP，防火墙放行）

---

## 六、常见问题

### 1）80 端口被占用 / 想用别的端口？
告警抓拍和录像的访问地址由后端固定按 **80 端口**拼接，**强烈建议让 80 空闲**，不要改端口，否则会出现“暂无抓拍”。
- 查谁占了 80：PowerShell 执行 `netstat -ano | findstr ":80 "`，再 `tasklist | findstr <PID>`。
- 常见是 IIS（万维网发布服务 W3SVC）或其它 nginx，停掉即可。
- 若确实无法释放 80，可改 `.env` 里 `WEB_PORT`，但那样抓拍图将无法直接显示（仅影响图片，不影响告警本身）。

### 2）页面能开但抓拍图裂了 / 拉不到流？
本系统用 `host.docker.internal` 让浏览器回到本机访问流媒体和图片。Docker Desktop 一般会自动解析；
若个别电脑不行，用记事本（管理员）打开
`C:\Windows\System32\drivers\etc\hosts`，末尾加一行：

```
127.0.0.1 host.docker.internal
```

保存后刷新浏览器。

### 3）启动后某容器一直 Restarting？
`docker compose logs sva-<服务名>` 看报错。最常见是上一次没正常退出，可：
```powershell
docker compose down -v
docker compose up -d
```
彻底重置后重新初始化（注意会清空之前的告警数据，属正常）。

### 4）数据存在哪？会不会占满 C 盘？
- 数据库、Redis、抓拍/录像分别在 Docker 命名卷 `mysql-data`、`redis-data`、`sva-upload`。
- 想清空抓拍录像：`docker compose down -v` 后重新 `up -d`。
- 想整体删除镜像释放空间：`docker compose down --rmi local`。

### 5）账号体系
默认管理员 `admin / admin123`（已关闭验证码，便于演示）。其它账号可在“系统管理 → 用户管理”里自行添加，权限按若依标准角色体系分配。

---

## 七、一句话总结

```powershell
docker load -i easysva-images-v1.0.tar   # 首次导入
docker compose up -d                      # 启动
# 浏览器开 http://localhost ，admin/admin123，启动 gbsleep001 看睡岗告警与抓拍
```
