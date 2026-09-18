#!/bin/bash
# easySVA 自动恢复脚本：WSL 重启后自动恢复 MediaServer 代理流 + 启动所有布控
LOG=/opt/SVA-dev/auto_recover.log
API="http://127.0.0.1:9114"
ZLM="http://127.0.0.1:9992"
SECRET="CHANGE_ME_ZLM_SECRET"

log(){ echo "$(date '+%F %T') $*" >> $LOG; }

# 1. 等待 MediaServer 就绪(最多 60 秒)
for i in $(seq 1 20); do
  if curl -s --max-time 2 "$ZLM/index/api/getServerConfig?secret=$SECRET" >/dev/null 2>&1; then
    log "MediaServer 已就绪"
    break
  fi
  sleep 3
done

# 2. 等待 test 流(ffmpeg 推流, 最多 30 秒)
for i in $(seq 1 10); do
  HAS=$(curl -s --max-time 3 "$ZLM/index/api/getMediaList?secret=$SECRET" | grep -Ec '"stream":"(sleep_)?test"')
  if [ "$HAS" -gt 0 ]; then log "test 流已就绪"; break; fi
  sleep 3
done

# 2.5 等待国标转桥流就绪(最多 40 秒)
for i in $(seq 1 20); do
  HAS=$(curl -s --max-time 3 "$ZLM/index/api/getMediaList?secret=$SECRET" | grep -Ec '"stream":"gb_')
  if [ "$HAS" -gt 0 ]; then log "国标转桥流已就绪"; break; fi
  sleep 2
done

# 3. 重建所有 DIRECT 设备代理流(幂等, ZLM 重启后代理丢失需重建)
mysql -uroot -pCHANGE_ME_DB_PASSWORD easySVA -N -e "SELECT ape_id,direct_source_url FROM h_device WHERE stream_source_type='DIRECT' AND direct_source_url IS NOT NULL AND direct_source_url!='';" 2>/dev/null | while read ID URL; do
  [ -z "$ID" ] && continue
  ENCURL=$(python3 -c "import urllib.parse,sys; print(urllib.parse.quote(sys.argv[1], safe=''))" "$URL")
  RES=$(curl -s --max-time 8 "$ZLM/index/api/addStreamProxy?secret=$SECRET&vhost=__defaultVhost__&app=live&stream=$ID&url=$ENCURL&enable_rtsp=1&enable_rtmp=1&enable_hls=1&enable_mp4=0")
  log "重建代理 $ID -> $(echo $RES | head -c 120)"
  sleep 1
done

# 4. 等待后端就绪(最多 120 秒)
for i in $(seq 1 40); do
  if curl -s --max-time 3 -X POST "$API/login" -H "Content-Type: application/json" -d '{"username":"admin","password":"admin123"}' >/dev/null 2>&1; then
    log "后端已就绪"
    break
  fi
  sleep 3
done

# 5. 登录拿 token
TOKEN=$(curl -s --max-time 8 -X POST "$API/login" -H "Content-Type: application/json" -d '{"username":"admin","password":"admin123"}' | python3 -c "import json,sys; print(json.load(sys.stdin).get('token',''))" 2>/dev/null)
if [ -z "$TOKEN" ]; then
  log "登录失败，退出"
  exit 1
fi

# 6. 获取所有布控 ID
IDS=$(curl -s --max-time 8 "$API/deployments" -H "Authorization: Bearer $TOKEN" | python3 -c "
import json,sys
try:
    d=json.load(sys.stdin)
    for t in d.get('data',[]):
        print(t.get('deploymentId',''))
except Exception: pass
")

# 7. 逐个启动布控(幂等并带重试)
for ID in $IDS; do
  RES=$(curl -s --max-time 30 -X POST "$API/deployments/$ID/start" -H "Authorization: Bearer $TOKEN")
  log "start $ID -> $(echo $RES | head -c 200)"
  if echo "$RES" | grep -q '"success":false'; then
    sleep 5
    RES=$(curl -s --max-time 30 -X POST "$API/deployments/$ID/start" -H "Authorization: Bearer $TOKEN")
    log "retry start $ID -> $(echo $RES | head -c 200)"
  fi
done
log "自动恢复完成"
