#!/bin/bash
set -e
echo "=================================================="
echo "正在模拟国标设备下线（发送 SIP 注销包并停止推流）..."
echo "=================================================="

# 1. 停止模拟器进程（兼容 root 与普通用户）
if [ "$(id -u)" -eq 0 ]; then
    pkill -TERM -f "gb28181_device_simulator.py" 2>/dev/null || true
else
    sudo pkill -TERM -f "gb28181_device_simulator.py" 2>/dev/null || true
fi

sleep 1

# 2. 检查国标信令服务设备池状态
echo "正在核验 GbSipServer 在线设备池..."
DEVS=$(curl -s http://127.0.0.1:18080/gb28181/api/devices || echo '{"data":[]}')
echo "-> GbSipServer 返回设备: $DEVS"

# 3. 自动调用 Java 后端同步接口，将离线状态写回数据库
echo "正在调用后端同步接口刷新数据库..."
TOKEN=$(curl -s -X POST "http://127.0.0.1:9114/login" -H "Content-Type: application/json" -d '{"username":"admin","password":"admin123"}' | python3 -c "import json,sys; print(json.load(sys.stdin).get('token',''))" 2>/dev/null)
if [ -n "$TOKEN" ]; then
  SYNC_RES=$(curl -s -X POST "http://127.0.0.1:9114/waring/device/gb28181/sync" -H "Authorization: Bearer $TOKEN")
  echo "-> 后端状态同步结果: $SYNC_RES"
else
  echo "-> [WARN] 登录后端失败，无法自动同步"
fi

# 4. 检查当前数据库实际状态
DB_STATE=$(mysql -uroot -peasySVA.EZ easySVA -N -e "SELECT is_online FROM h_device WHERE device_type='gb28181' LIMIT 1;" 2>/dev/null || echo "未知")
echo "=================================================="
if [ "$DB_STATE" = "0" ]; then
  echo ">> 【成功】数据库中设备状态已变为：0（离线）！"
  echo ">> 请在前端「设备管理」页面点击【同步国标设备】或按 F5 刷新查看红色的【离线】标签！"
else
  echo ">> 数据库中当前状态为: $DB_STATE"
fi
echo "=================================================="
