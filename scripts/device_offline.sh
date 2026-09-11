#!/bin/bash
# ============================================================
# easySVA 国标 GB28181 设备下线脚本
# 支持指定设备或全部设备下线：
#   bash device_offline.sh 1    # 下线设备1 (西门高精度球机)
#   bash device_offline.sh 2    # 下线设备2 (工位电脑摄像头)
#   bash device_offline.sh all  # 下线全部国标设备 (默认)
# ============================================================

TARGET="${1:-all}"
BASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "=================================================="
echo "执行国标设备下线模拟 (目标: $TARGET)..."
echo "=================================================="

kill_dev1() {
    echo "-> 正在下线设备1 (西门球机, device_id: 34020000001320000001, port: 15060)..."
    pkill -TERM -f "device-id 34020000001320000001" 2>/dev/null || true
    pkill -TERM -f "device-port 15060" 2>/dev/null || true
}

kill_dev2() {
    echo "-> 正在下线设备2 (电脑摄像头, device_id: 34020000001320000003, port: 15062)..."
    # 清理可能存在的 guard 守护进程
    if [ -f /tmp/webcam_dev2.guard.pid ]; then
        kill -TERM "$(cat /tmp/webcam_dev2.guard.pid 2>/dev/null)" 2>/dev/null || true
        rm -f /tmp/webcam_dev2.guard.pid
    fi
    pkill -f "webcam_gb_online.sh" 2>/dev/null || true
    pkill -TERM -f "device-id 34020000001320000003" 2>/dev/null || true
    pkill -TERM -f "device-port 15062" 2>/dev/null || true
}

case "$TARGET" in
    1)
        kill_dev1
        ;;
    2)
        kill_dev2
        ;;
    all)
        kill_dev1
        kill_dev2
        pkill -TERM -f "gb28181_device_simulator.py" 2>/dev/null || true
        ;;
    *)
        echo "参数错误: $TARGET。用法: bash $0 [1|2|all]"
        exit 1
        ;;
esac

# 等待模拟器发送 SIP REGISTER (Expires: 0) 注销握手
echo "等待 SIP 注销包处理..."
sleep 1.5

# 核验 GbSipServer 在线设备池
echo "正在核验 GbSipServer 在线设备池..."
DEVS=$(curl -s http://127.0.0.1:18080/gb28181/api/devices || echo '{"data":[]}')
echo "-> GbSipServer 当前在线设备: $DEVS"

# 自动调用 Java 后端同步接口，将离线状态写回数据库
echo "正在调用后端同步接口刷新数据库..."
TOKEN=$(curl -s -X POST "http://127.0.0.1:9114/login" -H "Content-Type: application/json" -d '{"username":"admin","password":"admin123"}' | python3 -c "import json,sys; print(json.load(sys.stdin).get('token',''))" 2>/dev/null)
if [ -n "$TOKEN" ]; then
    SYNC_RES=$(curl -s -X POST "http://127.0.0.1:9114/waring/device/gb28181/sync" -H "Authorization: Bearer $TOKEN")
    echo "-> 后端状态同步结果: $SYNC_RES"
else
    echo "-> [WARN] 登录后端失败，无法自动同步"
fi

echo "=================================================="
echo ">> 数据库中所有国标设备当前状态如下："
mysql -uroot -peasySVA.EZ easySVA -e "SELECT name AS '设备名称', gb_device_id AS '国标编码', port AS '端口', CASE is_online WHEN '1' THEN '在线 (1)' WHEN '0' THEN '离线 (0)' ELSE is_online END AS '在线状态', last_keepalive_at AS '最近心跳' FROM h_device WHERE device_type='gb28181';" 2>/dev/null || true
echo "=================================================="
echo "提示: 可在前端「设备管理」页面点击【同步国标设备】或按 F5 刷新查看状态"
