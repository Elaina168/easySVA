#!/bin/bash
set -e
echo "=================================================="
echo "正在模拟国标设备通电并注册上线..."
echo "=================================================="

VIDEO_SRC="/opt/SVA-dev/media/sleep_source.mp4"

# 使用 sudo bash -c 执行，确保重定向日志文件时不会发生 Permission denied
sudo bash -c "nohup python3 /opt/SVA-dev/SVA-backend/SVA-mediaServer/gb28181/tools/gb28181_device_simulator.py \
  --device-id 34020000001320000001 \
  --channel-id 34020000001320000002 \
  --platform-id 34020000002000000001 \
  --password 12345678 \
  --realm 3402000000 \
  --input '$VIDEO_SRC' \
  --heartbeat-interval 15 > /opt/SVA-dev/simulator.log 2>&1 &"

# 等待模拟器完成 SIP 注册握手
sleep 2

# 核验 GbSipServer 在线设备池
echo "正在核验 GbSipServer 在线设备池..."
DEVS=$(curl -s http://127.0.0.1:18080/gb28181/api/devices || echo '{"data":[]}')
echo "-> GbSipServer 返回设备: $DEVS"

# 调用 Java 后端同步接口，将在线状态写回数据库
echo "正在调用后端同步接口刷新数据库..."
TOKEN=$(curl -s -X POST "http://127.0.0.1:9114/login" -H "Content-Type: application/json" -d '{"username":"admin","password":"admin123"}' | python3 -c "import json,sys; print(json.load(sys.stdin).get('token',''))" 2>/dev/null)
if [ -n "$TOKEN" ]; then
  SYNC_RES=$(curl -s -X POST "http://127.0.0.1:9114/waring/device/gb28181/sync" -H "Authorization: Bearer $TOKEN")
  echo "-> 后端状态同步结果: $SYNC_RES"
fi

DB_STATE=$(mysql -uroot -peasySVA.EZ easySVA -N -e "SELECT is_online FROM h_device WHERE device_type='gb28181' LIMIT 1;" 2>/dev/null || echo "未知")
echo "=================================================="
if [ "$DB_STATE" = "1" ]; then
  echo ">> 【成功】数据库中设备状态已恢复为：1（在线）！"
  echo ">> 请在前端「设备管理」页面点击【同步国标设备】或按 F5 刷新查看绿色的【在线】标签！"
else
  echo ">> 数据库中当前状态为: $DB_STATE"
fi
echo "=================================================="
