#!/bin/bash
# ============================================================
# easySVA 国标 GB28181 设备上线脚本
# 支持指定设备或全部设备上线：
#   bash device_online.sh 1    # 上线设备1 (西门高精度球机)
#   bash device_online.sh 2    # 上线设备2 (工位电脑摄像头)
#   bash device_online.sh all  # 上线全部国标设备 (默认)
# ============================================================

TARGET="${1:-all}"
BASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SIM_PY="$BASE_DIR/SVA-backend/SVA-mediaServer/gb28181/tools/gb28181_device_simulator.py"
VIDEO_SRC="$BASE_DIR/media/sleep_source.mp4"

echo "=================================================="
echo "执行国标设备通电注册上线 (目标: $TARGET)..."
echo "=================================================="

start_dev1() {
    echo "-> 正在上线设备1 (西门球机, device_id: 34020000001320000001)..."
    if pgrep -f "device-id 34020000001320000001" >/dev/null; then
        echo "  [提示] 设备1 模拟器已在运行中 (PID: $(pgrep -f 'device-id 34020000001320000001' | tr '\n' ' '))"
    else
        nohup python3 "$SIM_PY"           --device-id 34020000001320000001           --channel-id 34020000001320000002           --channel-name "西门高精度国标球机(本地视频源)"           --platform-id 34020000002000000001           --password 12345678           --realm 3402000000           --device-port 15060           --media-source-port 30000           --input "$VIDEO_SRC"           --heartbeat-interval 15 > "$BASE_DIR/simulator.log" 2>&1 &
        echo "  设备1 已启动，PID: $!"
    fi
}

start_dev2() {
    echo "-> 正在上线设备2 (工位电脑摄像头, device_id: 34020000001320000003)..."
    # 检查 webcam_bridge (:18090) 是否运行
    if ! ss -ltn 2>/dev/null | grep -q ':18090'; then
        echo "  启动电脑摄像头推流中继 (webcam_bridge :18090)..."
        nohup sudo -u erqi python3 "$BASE_DIR/webcam_bridge.py" >> "$BASE_DIR/webcam_bridge.log" 2>&1 &
        sleep 1
    fi

    if pgrep -f "device-id 34020000001320000003" >/dev/null; then
        echo "  [提示] 设备2 模拟器已在运行中 (PID: $(pgrep -f 'device-id 34020000001320000003' | tr '\n' ' '))"
    else
        nohup python3 "$SIM_PY"           --device-id 34020000001320000003           --channel-id 34020000001320000004           --channel-name "工位实景国标摄像头(电脑摄像头)"           --platform-id 34020000002000000001           --password 12345678           --realm 3402000000           --device-port 15062           --media-source-port 30002           --input "rtsp://127.0.0.1:9994/live/webcam"           --heartbeat-interval 15 > "$BASE_DIR/simulator_webcam.log" 2>&1 &
        echo "  设备2 已启动，PID: $!"
    fi
}

case "$TARGET" in
    1)
        start_dev1
        ;;
    2)
        start_dev2
        ;;
    all)
        start_dev1
        start_dev2
        ;;
    *)
        echo "参数错误: $TARGET。用法: bash $0 [1|2|all]"
        exit 1
        ;;
esac

# 等待模拟器完成 SIP 注册握手
echo "等待模拟器完成 SIP 注册握手..."
sleep 2

# 核验 GbSipServer 在线设备池
echo "正在核验 GbSipServer 在线设备池..."
DEVS=$(curl -s http://127.0.0.1:18080/gb28181/api/devices || echo '{"data":[]}')
echo "-> GbSipServer 当前在线设备: $DEVS"

# 调用 Java 后端同步接口，将在线状态写回数据库
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
mysql -uroot -pCHANGE_ME_DB_PASSWORD easySVA -e "SELECT name AS '设备名称', gb_device_id AS '国标编码', port AS '端口', CASE is_online WHEN '1' THEN '在线 (1)' WHEN '0' THEN '离线 (0)' ELSE is_online END AS '在线状态', last_keepalive_at AS '最近心跳' FROM h_device WHERE device_type='gb28181';" 2>/dev/null || true
echo "=================================================="
echo "提示: 可在前端「设备管理」页面点击【同步国标设备】或按 F5 刷新查看状态"
