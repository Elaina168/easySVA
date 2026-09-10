#!/bin/bash
# 启动第二个独立国标设备：电脑摄像头模拟器 (34020000001320000003)
set -e

SIM_PY="/opt/SVA-dev/SVA-backend/SVA-mediaServer/gb28181/tools/gb28181_device_simulator.py"
LOG_FILE="/opt/SVA-dev/simulator_webcam.log"

# 杀死旧的 15062 模拟器进程
pkill -f "device-port 15062" 2>/dev/null || true
sleep 1

echo "正在启动独立国标设备 2 (电脑摄像头)..."
nohup python3 "$SIM_PY" \
  --device-id 34020000001320000003 \
  --channel-id 34020000001320000004 \
  --channel-name "工位实景国标摄像头" \
  --device-port 15062 \
  --media-source-port 30002 \
  --heartbeat-interval 15.0 \
  --input "rtsp://127.0.0.1:9994/live/webcam" >> "$LOG_FILE" 2>&1 &

echo "国标设备 2 已启动，PID: $!"
