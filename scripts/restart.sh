#!/bin/bash
# easySVA 全系统一键重启与状态自愈脚本
set -e

LOG="/opt/SVA-dev/restart.log"
exec > >(tee -a "$LOG") 2>&1
echo "========================================================"
echo "easySVA 全系统服务重启开始: $(date '+%F %T')"
echo "========================================================"

# 1. 优雅停止所有关联服务
echo "[1/7] 停止现有所有易构服务..."
pkill -f "gb_bridge_daemon" 2>/dev/null || true
pkill -f "webcam_bridge" 2>/dev/null || true
pkill -f "gb28181_device_simulator.py" 2>/dev/null || true
pkill -f "ffmpeg.*sleep_test" 2>/dev/null || true
pkill -f "ffmpeg.*0100000016" 2>/dev/null || true
pkill -f "ffmpeg.*34020000001320000003" 2>/dev/null || true
pkill -f "GbSipServer" 2>/dev/null || true
pkill -f "Analyzer" 2>/dev/null || true
pkill -f "backend.jar" 2>/dev/null || true
pkill -f "MediaServer" 2>/dev/null || true

sleep 3

# 强制释放关键端口（若有残留）
for PORT in 9114 9992 9993 9994 9995 18080; do
  fuser -k -9 ${PORT}/tcp 2>/dev/null || true
done
fuser -k -9 5060/udp 2>/dev/null || true
fuser -k -9 5060/tcp 2>/dev/null || true
sleep 1

# 2. 启动 MediaServer (ZLM)
echo "[2/7] 启动 ZLMediaKit (MediaServer)..."
cd /opt/SVA-dev/mediaServer
nohup ./MediaServer -d > /opt/SVA-dev/mediaServer/zlm_boot.log 2>&1 &

for i in $(seq 1 20); do
  if curl -s --max-time 2 "http://127.0.0.1:9992/index/api/getServerConfig?secret=V3522025zlm0aA9ajn7UiOWi" >/dev/null 2>&1; then
    echo "  -> MediaServer 已就绪 (端口 9992/9994/9995)"
    break
  fi
  sleep 1
done

# 3. 启动 GbSipServer (国标 SIP 信令服务)
echo "[3/7] 启动 GbSipServer..."
cd /opt/SVA-dev/SVA-backend/SVA-mediaServer/release/linux/Release
export EASY_SVA_ZLM_API_SECRET=V3522025zlm0aA9ajn7UiOWi
nohup ./GbSipServer > /opt/SVA-dev/mediaServer/gbsip.log 2>&1 &

for i in $(seq 1 20); do
  if curl -s --max-time 2 "http://127.0.0.1:18080/gb28181/api/devices" >/dev/null 2>&1; then
    echo "  -> GbSipServer 已就绪 (端口 18080 / SIP 5060)"
    break
  fi
  sleep 1
done

# 4. 启动 FFmpeg 睡岗测试流与国标设备模拟器
echo "[4/7] 启动 FFmpeg sleep_test 流、GB28181 模拟器与国标动态转桥..."
VIDEO_SRC="/opt/SVA-dev/media/sleep_source.mp4"
if [ -f "$VIDEO_SRC" ]; then
  nohup ffmpeg -re -stream_loop -1 -i "$VIDEO_SRC" -c:v libx264 -preset ultrafast -tune zerolatency -an -f flv rtmp://127.0.0.1:9995/live/sleep_test >/dev/null 2>&1 &
  echo "  -> FFmpeg sleep_test 推流已启动"
  
  nohup python3 /opt/SVA-dev/SVA-backend/SVA-mediaServer/gb28181/tools/gb28181_device_simulator.py     --device-id 34020000001320000001     --channel-id 34020000001320000002     --channel-name "西门高精度国标球机"     --platform-id 34020000002000000001     --password 12345678     --realm 3402000000     --device-port 15060     --media-source-port 30000     --input "$VIDEO_SRC"     --heartbeat-interval 15 > /opt/SVA-dev/simulator.log 2>&1 &
  echo "  -> GB28181 设备1 (西门球机 :15060) 已启动"

  # 启动电脑摄像头 WebSocket 转推中继
  nohup sudo -u erqi python3 /opt/SVA-dev/webcam_bridge.py >> /opt/SVA-dev/webcam_bridge.log 2>&1 &
  echo "  -> 电脑摄像头推流中继 (webcam_bridge :18090) 已启动"

  # 启动独立国标设备2 (电脑摄像头模拟器 :15062)
  nohup python3 /opt/SVA-dev/SVA-backend/SVA-mediaServer/gb28181/tools/gb28181_device_simulator.py     --device-id 34020000001320000003     --channel-id 34020000001320000004     --channel-name "工位实景国标摄像头(电脑摄像头)"     --platform-id 34020000002000000001     --password 12345678     --realm 3402000000     --device-port 15062     --media-source-port 30002     --input "rtsp://127.0.0.1:9994/live/webcam"     --heartbeat-interval 15 > /opt/SVA-dev/simulator_webcam.log 2>&1 &
  echo "  -> GB28181 设备2 (电脑摄像头 :15062) 已启动"
  
  nohup /opt/SVA-dev/gb_bridge_daemon.sh >/dev/null 2>&1 &
  echo "  -> GB28181 国标动态转桥守护进程已启动"
else
  echo "  [WARN] 视频源不存在: $VIDEO_SRC"
fi

# 5. 启动 C++ Analyzer 分析引擎
echo "[5/7] 启动 C++ Analyzer 分析服务..."
cd /opt/SVA-dev/server
nohup ./Analyzer -f /opt/SVA-dev/server/config.json > /opt/SVA-dev/server/log.out 2>&1 &

for i in $(seq 1 20); do
  if curl -s --max-time 2 "http://127.0.0.1:9993/api/health" >/dev/null 2>&1; then
    echo "  -> Analyzer 分析引擎已就绪 (端口 9993)"
    break
  fi
  sleep 1
done

# 6. 启动 Java Spring Boot 后端
echo "[6/7] 启动 Java 后端 (ruoyi / backend.jar)..."
cd /opt/SVA-dev/backend
nohup java -jar backend.jar > /opt/SVA-dev/backend/log.out 2>&1 &

for i in $(seq 1 45); do
  if curl -s --max-time 3 -X POST "http://127.0.0.1:9114/login" -H "Content-Type: application/json" -d '{"username":"admin","password":"admin123"}' >/dev/null 2>&1; then
    echo "  -> Java 后端已就绪 (端口 9114)"
    break
  fi
  sleep 2
done

# 7. 调用自动恢复脚本恢复媒体代理与布控
echo "=== [7/7] 执行布控任务自愈恢复 (auto_recover.sh) ==="
bash /opt/SVA-dev/auto_recover.sh

echo "========================================================"
echo "easySVA 全系统服务重启完成: $(date '+%F %T')"
echo "========================================================"
