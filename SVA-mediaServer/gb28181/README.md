# easySVA GB28181 信令与设备模拟器运行手册

这套实现把“设备接入”和“视频收流”拆成两个职责明确的进程。`GbSipServer` 负责 SIP 信令、设备状态和点播编排；原有 `MediaServer` 继续作为 ZLMediaKit 流媒体内核，接收设备发来的 PS/RTP，并把它转换成 RTSP、RTMP、HTTP-FLV 等播放协议。

本目录还提供一个纯 Python 的可复现设备。它会像网络摄像机一样主动注册、完成 Digest 鉴权、定时发送 Keepalive、上报 Catalog，并在收到 INVITE 后启动 FFmpeg 测试画面，将 MPEG-PS 封装为 RTP/96 推给 ZLMediaKit。它不是绕过协议直接向 RTSP 推流，因此可以用来观察完整的 GB28181 通路。

## 一、先理解这条链路

完整流程如下：

```text
模拟设备                 GbSipServer                    MediaServer/ZLMediaKit
   | REGISTER ---------------->|                                  |
   |<------------- 401 + nonce |                                  |
   | REGISTER + Digest ------->|                                  |
   |<------------------- 200 OK|                                  |
   | MESSAGE Keepalive ------->|                                  |
   | MESSAGE Catalog --------->|                                  |
   |                            |--- openRtpServer HTTP API ------>|
   |<-------------------- INVITE + SDP（RTP 地址、端口、SSRC）    |
   | 200 OK + SDP ------------>|                                  |
   |<----------------------- ACK|                                  |
   |====================== MPEG-PS over RTP ======================>|
   |                            |<==== RTSP/RTMP/HTTP-FLV 播放 =====|
   |<----------------------- BYE|                                  |
   | 200 OK ------------------>|--- closeRtpServer HTTP API ----->|
```

这里最容易混淆的是 SIP 和 RTP。SIP 是控制面，只传注册、心跳、目录、点播与停止命令；真正的视频字节不走 SIP，而是走 RTP。SDP 是 INVITE 里的媒体说明，告诉设备“把什么格式的视频发到哪个 IP、端口，使用哪个 SSRC”。

几个核心术语与代码行为的对应关系如下：

| 术语 | 在本实现中的含义 |
| --- | --- |
| REGISTER | 设备主动向平台声明“我上线了”，也用于续期；`Expires: 0` 表示注销 |
| Digest | 密码不直接上网，设备利用用户名、realm、nonce、请求 URI 等计算 MD5 响应 |
| realm | GB28181 域编码，本示例是 10 位 `3402000000` |
| nonce | 平台在 401 响应中生成的一次性随机挑战，避免直接重放旧鉴权结果 |
| Keepalive | 设备周期发送的 `MESSAGE`，正文为 MANSCDP XML，用于保活和在线判断 |
| Catalog | 设备目录，描述设备下面有哪些通道、通道名称以及 `ON/OFF` 状态 |
| INVITE | 平台向某个通道发起实时点播 |
| SDP | INVITE/200 OK 中的媒体参数，例如 IP、端口、`PS/90000` 和 SSRC |
| ACK | 平台确认设备的 200 OK；模拟器收到 ACK 后才开始推送媒体 |
| BYE | 任意一端结束已建立的点播会话 |
| RTP | 承载实时媒体数据的网络包；本实现动态负载类型 PT 为 96 |
| SSRC | 一路 RTP 流的数字身份，设备发包值必须与 SDP 的 `y=` 一致 |
| MPEG-PS | GB28181 常用的节目流封装，里面承载 H.264 视频 |
| ZLMediaKit | 负责开放 RTP 接收端口、解析 PS，并转换为各种播放协议 |

## 二、编译

下面的命令都在 Ubuntu 中执行，仓库固定为真实开发目录：

```bash
cd /opt/SVA/easy-sva-mo/SVA-mediaServer

cmake -S . -B build-gb28181 \
  -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_GB28181_SIGNALING=ON \
  -DENABLE_TESTS=ON

cmake --build build-gb28181 -j"$(nproc)" \
  --target MediaServer GbSipServer \
  test_gb28181_sip_core \
  test_gb28181_service_core \
  test_gb28181_registration \
  test_gb28181_device_messages \
  test_gb28181_platform_commands \
  test_gb28181_media_session \
  test_gb28181_zlm_api \
  test_gb28181_sip_dialog \
  test_gb28181_live_service \
  test_gb28181_control_api
```

Release 产物位于 `release/linux/Release/`。模拟器需要 Python 3 和带 `libx264` 的 FFmpeg，可以先确认：

```bash
python3 --version
ffmpeg -hide_banner -version
```

## 三、准备配置

信令配置文件是 `conf/gb28181.ini`。本机演示可以使用默认的设备与通道编号：

```text
平台 ID：34020000002000000001
设备 ID：34020000001320000001
通道 ID：34020000001320000002
域编码： 3402000000
设备密码：12345678（仅用于本机演示）
```

部署到局域网时，需要把 `sip.advertised_ip` 和 `media.rtp_advertised_ip` 改成设备确实能够访问的 Ubuntu 地址，不能保留 `127.0.0.1`。`listen_ip=0.0.0.0` 只是监听所有网卡，不是可以写入 SIP/SDP 给设备访问的地址。

`GbSipServer` 调用 ZLMediaKit HTTP API 时必须提供密钥。不要把生产密钥写进 Git。可以从当前 MediaServer 配置读取 `[api]` 段的密钥并放进当前终端环境：

```bash
cd /opt/SVA/easy-sva-mo/SVA-mediaServer

export EASY_SVA_ZLM_API_SECRET="$(awk -F= '
  /^\[api\]$/ { in_api=1; next }
  /^\[/ { in_api=0 }
  in_api && /^secret=/ { print substr($0, 8); exit }
' release/linux/Release/config.ini)"

export EASY_SVA_GB_API_SECRET='gb-local-demo-secret'
```

第二个变量保护 easySVA 自己的控制 API。即使 API 只监听回环地址，本手册仍然启用 Bearer 密钥，以便运行方式和局域网部署一致。

## 四、启动完整服务

打开第一个 Ubuntu 终端，启动 ZLMediaKit：

```bash
cd /opt/SVA/easy-sva-mo/SVA-mediaServer/release/linux/Release
./MediaServer -c ./config.ini
```

打开第二个 Ubuntu 终端，重新执行上一节的两个 `export`，然后启动信令服务：

```bash
cd /opt/SVA/easy-sva-mo/SVA-mediaServer

./release/linux/Release/GbSipServer \
  --config ./conf/gb28181.ini
```

默认服务端口为：SIP UDP/TCP `5060`、GB 控制 API `18080`、ZLM HTTP `9992`、RTSP `9994`、RTMP `9995`。可先验证控制面：

```bash
curl -sS http://127.0.0.1:18080/gb28181/api/health
```

## 五、启动可复现设备

打开第三个 Ubuntu 终端：

```bash
cd /opt/SVA/easy-sva-mo/SVA-mediaServer

python3 ./gb28181/tools/gb28181_device_simulator.py \
  --platform-host 127.0.0.1 \
  --platform-port 5060 \
  --advertised-ip 127.0.0.1 \
  --device-port 15060 \
  --password 12345678 \
  --heartbeat-interval 30
```

模拟器启动后会依次打印注册、心跳和目录上报结果。默认没有结束时间，按 `Ctrl+C` 时会停止媒体并发送 `Expires: 0` 的 REGISTER 注销。

如果希望把自有视频作为设备画面，可增加：

```bash
--input /绝对路径/测试视频.mp4
```

未传 `--input` 时，FFmpeg 会生成 640×360、25 帧的测试图案。

## 六、通过控制 API 点播

以下命令在第四个终端执行：

```bash
export GB_API_SECRET='gb-local-demo-secret'
DEVICE_ID='34020000001320000001'
CHANNEL_ID='34020000001320000002'
```

查看注册设备：

```bash
curl -sS \
  -H "Authorization: Bearer ${GB_API_SECRET}" \
  http://127.0.0.1:18080/gb28181/api/devices
```

主动查询目录，再查看通道：

```bash
curl -sS -X POST \
  -H "Authorization: Bearer ${GB_API_SECRET}" \
  -H 'Content-Type: application/json' \
  -d "{\"device_id\":\"${DEVICE_ID}\"}" \
  http://127.0.0.1:18080/gb28181/api/catalog/query

curl -sS \
  -H "Authorization: Bearer ${GB_API_SECRET}" \
  "http://127.0.0.1:18080/gb28181/api/catalog?device_id=${DEVICE_ID}"
```

开始实时点播，并自动取出会话编号：

```bash
START_JSON="$(curl -sS -X POST \
  -H "Authorization: Bearer ${GB_API_SECRET}" \
  -H 'Content-Type: application/json' \
  -d "{\"device_id\":\"${DEVICE_ID}\",\"channel_id\":\"${CHANNEL_ID}\"}" \
  http://127.0.0.1:18080/gb28181/api/live/start)"

printf '%s\n' "${START_JSON}"

SESSION_ID="$(printf '%s' "${START_JSON}" | \
  python3 -c 'import json,sys; print(json.load(sys.stdin)["data"]["session_id"])')"
```

等待一两秒后查询会话；`state` 应从 `preparing`、`inviting` 进入 `streaming`：

```bash
curl -sS \
  -H "Authorization: Bearer ${GB_API_SECRET}" \
  http://127.0.0.1:18080/gb28181/api/sessions
```

从返回结果中取得 `stream_id`，即可播放。当前仓库配置关闭了 HLS，所以本机验收优先使用 RTSP：

```bash
ffplay -rtsp_transport tcp \
  'rtsp://127.0.0.1:9994/rtp/这里替换为stream_id'
```

也可以使用以下地址：

```text
RTMP：    rtmp://127.0.0.1:9995/rtp/<stream_id>
HTTP-FLV：http://127.0.0.1:9992/rtp/<stream_id>.live.flv
```

停止点播：

```bash
curl -sS -X POST \
  -H "Authorization: Bearer ${GB_API_SECRET}" \
  -H 'Content-Type: application/json' \
  -d "{\"session_id\":\"${SESSION_ID}\"}" \
  http://127.0.0.1:18080/gb28181/api/live/stop
```

会话最终应变成 `stopped`，ZLMediaKit 对应的 RTP 接收端口也会被释放。

## 七、自动化验证

模拟器协议和 RTP 封包单元测试：

```bash
cd /opt/SVA/easy-sva-mo/SVA-mediaServer
python3 ./gb28181/tests/test_device_simulator.py
```

端到端回归会临时启动 `GbSipServer`、模拟 ZLM 的端口分配接口和真实 UDP 接收器，然后自动完成注册、心跳、目录、点播、PS/RTP 收包和停流，不依赖已经运行的服务：

```bash
cd /opt/SVA/easy-sva-mo/SVA-mediaServer

python3 ./gb28181/tests/smoke_device_simulator.py \
  --server ./release/linux/Release/GbSipServer \
  --ffmpeg /usr/local/bin/ffmpeg
```

成功时最后一行是：

```text
GB28181 reproducible device end-to-end smoke passed
```

需要连同真实 ZLMediaKit 的 PS 解复用、RTSP 输出和视频解码一起验收时，执行：

```bash
python3 ./gb28181/tests/smoke_real_zlm.py \
  --server ./release/linux/Release/GbSipServer \
  --media-server ./release/linux/Release/MediaServer \
  --ffmpeg /usr/local/bin/ffmpeg \
  --ffprobe /usr/local/bin/ffprobe
```

这个测试使用临时配置和动态端口，不读取或改写生产密钥。它会要求 ZLMediaKit 真正生成 RTSP 流，并通过 FFmpeg 解码 10 帧，而不只是检查 RTP UDP 包是否到达。上面的命令验证 UDP；平台主动建立 TCP/RTP 连接的模式用下面的命令验证：

```bash
python3 ./gb28181/tests/smoke_real_zlm.py \
  --server ./release/linux/Release/GbSipServer \
  --media-server ./release/linux/Release/MediaServer \
  --ffmpeg /usr/local/bin/ffmpeg \
  --ffprobe /usr/local/bin/ffprobe \
  --rtp-tcp-mode 2
```

## 八、目前的边界

当前生产路径完整支持 SIP over UDP/TCP，媒体接收支持 RTP over UDP、TCP 被动和 TCP 主动三种模式。模式 `2` 会在设备返回 `setup:passive` 的 SDP 后调用 ZLMediaKit `connectRtpServer`；连接成功前会话不会进入 `streaming`。配套模拟器可以验证 UDP 和平台主动 TCP 两条路径，暂不模拟“设备主动连接平台”的 TCP 被动媒体模式。

本实现负责的是实时点播主链路，不包含录像检索、回放、云台控制、报警订阅、语音对讲和国标级联。生产部署还需要按网络拓扑配置防火墙、NAT 映射、设备独立密码和 HTTPS/API 访问控制。
