#!/bin/bash
# easySVA 国标流动态转换桥守护进程
# 自动发现/启动 GB28181 会话，转推为 live 格式供布控分析与前端平滑播放

TARGET_STREAM="gb_34020000001320000002_0100000016"
LOG="/opt/SVA/gb_bridge.log"

get_stream_id() {
    python3 -c "
import urllib.request, json
try:
    # 1. 检查 ZLM 实际正在分发的 rtp 流
    zlm_url = 'http://127.0.0.1:9992/index/api/getMediaList?secret=V3522025zlm0aA9ajn7UiOWi&app=rtp'
    with urllib.request.urlopen(zlm_url, timeout=3) as resp:
        zlm_data = json.loads(resp.read().decode('utf-8')).get('data', [])
        active_zlm_streams = {s.get('stream') for s in zlm_data if s.get('schema') == 'rtsp'}

    # 2. 检查 GB 会话
    req = urllib.request.Request('http://127.0.0.1:18080/gb28181/api/sessions')
    with urllib.request.urlopen(req, timeout=3) as resp:
        data = json.loads(resp.read().decode('utf-8')).get('data', [])
        for s in data:
            sid = s.get('stream_id', '')
            if s.get('state') == 'streaming' and sid in active_zlm_streams:
                print(sid)
                exit(0)

    # 若无活跃流则调用 live/start
    url = 'http://127.0.0.1:18080/gb28181/api/live/start'
    payload = {'device_id': '34020000001320000001', 'channel_id': '34020000001320000002'}
    req2 = urllib.request.Request(url, data=json.dumps(payload).encode('utf-8'), headers={'Content-Type': 'application/json'})
    with urllib.request.urlopen(req2, timeout=5) as resp2:
        res = json.loads(resp2.read().decode('utf-8'))
        print(res.get('data', {}).get('stream_id', ''))
except Exception:
    pass
"
}

while true; do
    STREAM_ID=$(get_stream_id)
    if [ -z "$STREAM_ID" ]; then
        echo "[$(date '+%F %T')] 等待 GB28181 流就绪..." >> "$LOG"
        sleep 2
        continue
    fi

    echo "[$(date '+%F %T')] 启动 ffmpeg 转桥: rtp/$STREAM_ID -> live/$TARGET_STREAM" >> "$LOG"
    ffmpeg \
      -hide_banner -loglevel warning \
      -allowed_media_types video \
      -fflags +genpts \
      -rtsp_transport tcp \
      -i "rtsp://127.0.0.1:9994/rtp/$STREAM_ID" \
      -c:v libx264 -preset ultrafast -tune zerolatency \
      -b:v 1500k -maxrate 2000k -bufsize 3000k \
      -g 50 -keyint_min 25 \
      -fps_mode cfr -r 25 -an \
      -f flv "rtmp://127.0.0.1:9995/live/$TARGET_STREAM" \
      >> "$LOG" 2>&1
    
    EXIT_CODE=$?
    echo "[$(date '+%F %T')] ffmpeg 退出 (code=$EXIT_CODE)，2秒后重新检测并重启" >> "$LOG"
    sleep 2
done
