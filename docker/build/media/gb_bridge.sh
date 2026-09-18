#!/bin/bash
# 国标流动态转桥：发现 GB28181 rtp 流后用 ffmpeg 转成 live/rtmp，供分析与前端平滑播放
# media 容器内 ZLM 与 GbSip 都在 127.0.0.1
ZLM_SECRET="${ZLM_SECRET:-__INJECTED_AT_RUNTIME__}"
DEVICE_ID="${GB_DEVICE_ID:-34020000001320000001}"
CHANNEL_ID="${GB_CHANNEL_ID:-34020000001320000002}"
TARGET_STREAM="${TARGET_STREAM:-gb_34020000001320000002_0100000016}"
LOG=/opt/media/bridge_inner.log

get_stream_id() {
  python3 - "$ZLM_SECRET" "$DEVICE_ID" "$CHANNEL_ID" <<'PY'
import sys, urllib.request, json
secret, dev, ch = sys.argv[1], sys.argv[2], sys.argv[3]
try:
    with urllib.request.urlopen(
        f'http://127.0.0.1:9992/index/api/getMediaList?secret={secret}&app=rtp', timeout=3) as r:
        active = {s.get('stream') for s in json.loads(r.read().decode()).get('data', []) if s.get('schema')=='rtsp'}
    req = urllib.request.Request('http://127.0.0.1:18080/gb28181/api/sessions', headers={'Authorization': 'Bearer ' + secret})
    with urllib.request.urlopen(req, timeout=3) as r:
        for s in json.loads(r.read().decode()).get('data', []):
            sid = s.get('stream_id','')
            if s.get('state')=='streaming' and sid in active:
                print(sid); sys.exit(0)
    payload = json.dumps({'device_id': dev, 'channel_id': ch}).encode()
    req2 = urllib.request.Request('http://127.0.0.1:18080/gb28181/api/live/start', data=payload,
                                  headers={'Content-Type':'application/json', 'Authorization': 'Bearer ' + secret})
    with urllib.request.urlopen(req2, timeout=5) as r:
        print(json.loads(r.read().decode()).get('data',{}).get('stream_id',''))
except Exception:
    pass
PY
}

while true; do
  STREAM_ID=$(get_stream_id)
  if [ -z "$STREAM_ID" ]; then
    echo "[$(date '+%F %T')] 等待 GB28181 流就绪..." >> "$LOG"
    sleep 2; continue
  fi
  echo "[$(date '+%F %T')] 转桥 rtp/$STREAM_ID -> live/$TARGET_STREAM" >> "$LOG"
  ffmpeg -hide_banner -loglevel warning \
    -allowed_media_types video -fflags +genpts -rtsp_transport tcp \
    -i "rtsp://127.0.0.1:9994/rtp/$STREAM_ID" \
    -c:v libx264 -preset ultrafast -tune zerolatency \
    -b:v 1500k -maxrate 2000k -bufsize 3000k -g 50 -keyint_min 25 \
    -vsync cfr -r 25 -an \
    -f flv "rtmp://127.0.0.1:9995/live/$TARGET_STREAM" >> "$LOG" 2>&1
  echo "[$(date '+%F %T')] ffmpeg 退出，2 秒后重试" >> "$LOG"
  sleep 2
done
