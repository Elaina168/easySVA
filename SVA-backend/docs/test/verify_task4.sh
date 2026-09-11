#!/bin/bash
# =============================================================
# easySVA 任务四：可重复执行的后端验证脚本
# 分支：feature/java-backend
# 前置：后端已部署并监听 9114；MariaDB 库 easySVA 可用
# 用法：
#   MYSQL_PASSWORD='实际密码' bash docs/test/verify_task4.sh
#   或先 export MYSQL_PASSWORD='实际密码' 再执行
# 说明：脚本会自动创建/清理测试数据，可重复执行；
#       任一步失败会以非 0 退出码结束。
#       数据库密码通过环境变量 MYSQL_PASSWORD 传入，不写入脚本与记录。
# =============================================================
set -u
B=${B:-http://127.0.0.1:9114}
MYSQL_USER=${MYSQL_USER:-root}
MYSQL_PASSWORD=${MYSQL_PASSWORD:?请通过环境变量 MYSQL_PASSWORD 提供数据库密码}
MYSQL="mysql -u${MYSQL_USER} -p${MYSQL_PASSWORD} easySVA"
ADMIN_PASSWORD=${ADMIN_PASSWORD:-admin123}
PASS=0
FAIL=0

ok()   { echo "  [PASS] $1"; PASS=$((PASS+1)); }
bad()  { echo "  [FAIL] $1"; FAIL=$((FAIL+1)); }
check(){ if [ "$1" = "$2" ]; then ok "$3"; else bad "$3 (期望=$2 实际=$1)"; fi; }

echo "=============================================="
echo " easySVA 任务四 后端验证开始  $(date '+%F %T')"
echo "=============================================="

# 0) 登录
TOKEN=$(curl -s -X POST "$B/login" -H 'Content-Type: application/json' \
  -d "{\"username\":\"admin\",\"password\":\"$ADMIN_PASSWORD\"}" \
  | python3 -c "import sys,json; print(json.load(sys.stdin).get('token',''))")
if [ -z "$TOKEN" ]; then
  echo "  [FAIL] 无法登录，终止"
  exit 1
fi
ok "登录获取 token"

# 1) 原 RTSP 设备列表（回归）
RTSP_CNT=$($MYSQL -N -e "SELECT COUNT(*) FROM h_device WHERE device_type='rtsp';" 2>/dev/null)
LIST_CODE=$(curl -s -o /dev/null -w '%{http_code}' "$B/waring/device/list?device_type=rtsp" -H "Authorization: Bearer $TOKEN")
check "$LIST_CODE" "200" "RTSP 设备列表接口返回 200 (现有 $RTSP_CNT 台)"

# 2) 国标同步接口可调用
SYNC=$(curl -s -X POST "$B/waring/device/gb28181/sync" -H "Authorization: Bearer $TOKEN")
if echo "$SYNC" | grep -q '"code":200'; then ok "国标同步接口调用成功"; else bad "国标同步接口 ($SYNC)"; fi

# 3) 离线状态同步：插入 2 条在线模拟 GB 设备 -> 同步后应全部置 0（ZLM 未启用 GB28181）
GID1=34020000001320000901
GID2=34020000001320000902
$MYSQL -e "DELETE FROM h_device WHERE ape_id IN ('verify_gb_1','verify_gb_2');" 2>/dev/null
$MYSQL -e "INSERT INTO h_device (ape_id, name, stream_source_type, device_type, gb_device_id, gb_platform_id, is_online, monitor_status, zlm_server_id, sva_server_id, create_time, update_time) VALUES
 ('verify_gb_1','验证国标设备1','DIRECT','gb28181','$GID1','34020000002000000001','1','STOPPED',1,1,NOW(),NOW()),
 ('verify_gb_2','验证国标设备2','DIRECT','gb28181','$GID2','34020000002000000001','1','STOPPED',1,1,NOW(),NOW());" 2>/dev/null
curl -s -X POST "$B/waring/device/gb28181/sync" -H "Authorization: Bearer $TOKEN" > /dev/null
OFF1=$($MYSQL -N -e "SELECT is_online FROM h_device WHERE ape_id='verify_gb_1';" 2>/dev/null)
OFF2=$($MYSQL -N -e "SELECT is_online FROM h_device WHERE ape_id='verify_gb_2';" 2>/dev/null)
check "$OFF1$OFF2" "00" "离线同步：未返回的 GB 设备置为离线"
# 幂等：再同步一次，数量不变
curl -s -X POST "$B/waring/device/gb28181/sync" -H "Authorization: Bearer $TOKEN" > /dev/null
CNT=$($MYSQL -N -e "SELECT COUNT(*) FROM h_device WHERE gb_device_id IN ('$GID1','$GID2');" 2>/dev/null)
check "$CNT" "2" "重复同步幂等（无重复新增）"
$MYSQL -e "DELETE FROM h_device WHERE ape_id IN ('verify_gb_1','verify_gb_2');" 2>/dev/null

# 4) 睡岗告警入库 + 查询
curl -s -X POST "$B/waring/waring/addFromSvaSimple" -H "Authorization: Bearer $TOKEN" -H 'Content-Type: application/json' \
  -d '{"control_code":"controls2NmNxDkWsgKX8","behavior_type":"sleep","alarm_type":"sleep","image_path":"/tmp/verify_sleep.jpg","alarm_time":"2026-09-02 23:10:00"}' > /dev/null
SLEEP_TYPE=$($MYSQL -N -e "SELECT alarm_type FROM h_waring WHERE picture_url='/tmp/verify_sleep.jpg' ORDER BY w_id DESC LIMIT 1;" 2>/dev/null)
check "$SLEEP_TYPE" "SVA_SLEEP" "睡岗告警识别为 SVA_SLEEP"
SLEEP_QUERY=$($MYSQL -N -e "SELECT COUNT(*) FROM h_waring WHERE alarm_type='SVA_SLEEP';" 2>/dev/null)
OK_QUERY=$(curl -s -o /dev/null -w '%{http_code}' "$B/waring/waring/list?alarm_type=SVA_SLEEP" -H "Authorization: Bearer $TOKEN")
check "$OK_QUERY" "200" "按 alarm_type=SVA_SLEEP 查询接口可用 (库内 $SLEEP_QUERY 条)"
$MYSQL -e "DELETE FROM h_waring WHERE picture_url='/tmp/verify_sleep.jpg';" 2>/dev/null

# 5) GB28181 设备 startMonitor 不走 DIRECT 代理（不依赖 direct_source_url）
APE=verify_gb_monitor
$MYSQL -e "DELETE FROM h_device WHERE ape_id='$APE';" 2>/dev/null
$MYSQL -e "INSERT INTO h_device (ape_id, name, stream_source_type, device_type, gb_device_id, play_url, is_online, monitor_status, zlm_server_id, sva_server_id, create_time, update_time) VALUES
 ('$APE','验证GB播放','DIRECT','gb28181','34020000001320000999','http://127.0.0.1:1935/gb/verify_gb_monitor.live.flv','1','STOPPED',1,1,NOW(),NOW());" 2>/dev/null
START=$(curl -s -X POST "$B/waring/device/monitor/$APE/start" -H "Authorization: Bearer $TOKEN")
if echo "$START" | grep -q '"code":200'; then ok "GB 设备 startMonitor 成功"; else bad "GB 设备 startMonitor ($START)"; fi
RUN=$($MYSQL -N -e "SELECT monitor_status FROM h_device WHERE ape_id='$APE';" 2>/dev/null)
check "$RUN" "RUNNING" "GB 设备启动后 monitor_status=RUNNING"
$MYSQL -e "DELETE FROM h_device WHERE ape_id='$APE';" 2>/dev/null

# 6) ZLM/GB 请求失败时保留原状态（不误置离线）—— 受控修改 ZLM host，结束后恢复
APE_F=verify_gb_fail
$MYSQL -e "DELETE FROM h_device WHERE ape_id='$APE_F';" 2>/dev/null
$MYSQL -e "INSERT INTO h_device (ape_id, name, stream_source_type, device_type, gb_device_id, is_online, monitor_status, zlm_server_id, sva_server_id, create_time, update_time) VALUES
 ('$APE_F','验证失败保留状态','DIRECT','gb28181','34020000001320000903','1','STOPPED',1,1,NOW(),NOW());" 2>/dev/null
OLD_HOST=$($MYSQL -N -e "SELECT host FROM zlm_server WHERE id=1 LIMIT 1;" 2>/dev/null)
if [ -n "$OLD_HOST" ]; then
  # 无论脚本正常/异常退出都恢复 ZLM host
  trap "$MYSQL -e \"UPDATE zlm_server SET host='$OLD_HOST' WHERE id=1;\" >/dev/null 2>&1" EXIT
  $MYSQL -e "UPDATE zlm_server SET host='127.0.0.254' WHERE id=1;" 2>/dev/null
  curl -s -X POST "$B/waring/device/gb28181/sync" -H "Authorization: Bearer $TOKEN" > /dev/null
  FAIL_ON=$($MYSQL -N -e "SELECT is_online FROM h_device WHERE ape_id='$APE_F';" 2>/dev/null)
  check "$FAIL_ON" "1" "ZLM 请求失败时保留原状态（不误置离线）"
  $MYSQL -e "UPDATE zlm_server SET host='$OLD_HOST' WHERE id=1;" 2>/dev/null
  trap - EXIT
else
  bad "无法读取 zlm_server.host，跳过失败场景"
fi
$MYSQL -e "DELETE FROM h_device WHERE ape_id='$APE_F';" 2>/dev/null

echo "=============================================="
echo " 结果：通过 $PASS 项，失败 $FAIL 项"
echo "=============================================="
[ "$FAIL" -eq 0 ] && exit 0 || exit 1
