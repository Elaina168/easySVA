#!/bin/bash
# ============================================================
# 国标 GB28181 设备专项监控（SIP 注册 / 心跳 / 点播会话 / 信令 / 云台PTZ）
# 每 INTERVAL 秒刷新；Ctrl+C 退出，只读不影响设备与服务。
#   bash /opt/SVA/gb_monitor.sh
#   ONESHOT=1 bash /opt/SVA/gb_monitor.sh   # 单帧自检
# SVA 本地服务(ZLM/后端/Analyzer/数据库等)请看 sva_monitor.sh
# ============================================================

INTERVAL=2
GBAPI="http://127.0.0.1:18080"
SIMLOG="/opt/SVA/simulator.log"
HB_TIMEOUT=45        # 心跳15s一次，超过45s(3周期)判异常

R=$'\033[31m'; G=$'\033[32m'; Y=$'\033[33m'; C=$'\033[36m'; B=$'\033[1m'; D=$'\033[2m'; N=$'\033[0m'
trap 'printf "\033[?25h\033[0m"; exit 0' INT TERM EXIT

draw(){
  local NOW_TS=$(date +%s) NOW=$(date '+%F %T')
  local DEV_JSON SESS_JSON
  DEV_JSON=$(curl -s --max-time 1 "$GBAPI/gb28181/api/devices" 2>/dev/null)
  SESS_JSON=$(curl -s --max-time 1 "$GBAPI/gb28181/api/sessions" 2>/dev/null)
  export DEV_JSON SESS_JSON NOW_TS HB_TIMEOUT

  # ---- 日志侧统计(设备视角信令) ----
  local hb_cnt reg_cnt ptz_cnt inv_cnt last_hb
  [[ -r "$SIMLOG" ]] || { echo "${R}模拟器日志不存在: $SIMLOG（国标设备可能未启动）${N}"; sleep 2; return; }
  hb_cnt=$(grep -c "heartbeat sent" "$SIMLOG" 2>/dev/null)
  reg_cnt=$(grep -c "SIP registered" "$SIMLOG" 2>/dev/null)
  ptz_cnt=$(grep -c "PTZ-CONTROL" "$SIMLOG" 2>/dev/null)
  inv_cnt=$(grep -c "INVITE accepted" "$SIMLOG" 2>/dev/null)
  last_hb=$(grep "heartbeat sent" "$SIMLOG" 2>/dev/null | tail -1 | grep -oE "SN=[0-9]+")

  printf '\033[2J\033[H'
  printf "${C}${B}════════════ 国标 GB28181 设备监控 %s 每%ds刷新 ════════════${N}\n" "$NOW" "$INTERVAL"

  # ---- 设备 + 会话(由python渲染，含颜色/倒计时) ----
  python3 - <<'PYEOF'
import os,json,time
G="\033[32m";Y="\033[33m";R="\033[31m";B="\033[1m";D="\033[2m";N="\033[0m";C="\033[36m"
now=int(os.environ["NOW_TS"]); hbt=int(os.environ["HB_TIMEOUT"])
def load(k):
    try: return json.loads(os.environ.get(k,"") or '{}').get("data") or []
    except Exception: return []
def ago(ts):
    if not ts: return "-"
    s=now-int(ts)
    if s<60: return f"{s}秒前"
    return f"{s//60}分{s%60}秒前"
devs=load("DEV_JSON"); sess=load("SESS_JSON")

print(f"{B}【国标设备注册 / 心跳】{N}")
if not devs:
    print(f"  {R}当前无注册设备{N}")
for x in devs:
    online=x.get("online",False); hb=x.get("last_heartbeat_at",0); age=now-hb if hb else -1
    if online and 0<=age<=hbt: tag=f"{G}● 在线{N}"
    elif online: tag=f"{Y}● 在线但心跳超时 {age}s{N}"
    else: tag=f"{R}● 离线{N}"
    exp=x.get("expires_at",0); left=exp-now if exp else -1
    print(f"  设备 {B}{x.get('device_id','?')}{N}  {tag}")
    print(f"    传输={x.get('transport','?')}  对端={x.get('peer_ip','?')}:{x.get('peer_port','?')}  UA={x.get('user_agent','?')}")
    print(f"    注册于 {ago(x.get('registered_at'))}    上次心跳 {G if 0<=age<=hbt else Y}{age}s 前{N}    注册有效期剩 {left}s")

print(f"{B}【SIP 点播会话(INVITE/ACK 推流)】{N}")
if not sess: print(f"  {D}无活动点播会话{N}")
for s in sess:
    st=s.get("state","?")
    stc=G if st=="streaming" else Y
    dur=now-int(s.get("created_at",now))
    print(f"  {stc}{st}{N}  {s.get('session_id','?')}")
    print(f"    通道={s.get('channel_id','?')} SSRC={s.get('ssrc','?')} RTP端口={s.get('rtp_port','?')} 流ID={s.get('stream_id','?')} 已播放 {dur}s")
PYEOF

  printf "${B}【设备侧信令统计】${N} 注册 %s 次 ｜ 心跳累计 %s 次(最新 %s) ｜ INVITE点播 %s 次 ｜ 云台PTZ %s 次\n" \
         "${reg_cnt:-0}" "${hb_cnt:-0}" "${last_hb:-无}" "${inv_cnt:-0}" "${ptz_cnt:-0}"

  printf "${B}【最近关键 SIP 信令(设备侧, 去心跳刷屏)】${N}\n"
  grep -vE "heartbeat sent" "$SIMLOG" 2>/dev/null | tail -8 | sed 's/^/  /'

  printf "${B}【最近云台 PTZ 指令】${N}\n"
  local ptz
  ptz=$(grep "PTZ-CONTROL" "$SIMLOG" 2>/dev/null | tail -4 | sed 's/.*PTZCmd=\([A-F0-9]*\), 动作=\(.*\), 水平.*/    [PTZ] \1  \2/')
  if [[ -n "$ptz" ]]; then echo "$ptz"; else printf "  ${D}暂无云台指令${N}\n"; fi

  printf "${D}──────────────────────────────────────────────────────────────────${N}\n"
  # 总体判定
  local bad
  bad=$(echo "$DEV_JSON" | python3 -c "
import sys,json,time
try:
  now=int(time.time()); d=json.load(sys.stdin).get('data') or []
  print('0' if any(x.get('online') and (now-x.get('last_heartbeat_at',0))<=$HB_TIMEOUT for x in d) else '1')
except Exception: print('1')" 2>/dev/null)
  if [[ "$bad" == "0" ]]; then printf "${G}${B}状态：● 国标设备在线且心跳正常${N}\n"
  else printf "${Y}${B}状态：⚠ 未检测到心跳正常的国标设备（检查模拟器/网络）${N}\n"; fi
  printf "${D}Ctrl+C 关闭（只读）｜SVA 本地服务请看 sva_monitor.sh${N}\n"
}

if [[ "${ONESHOT:-0}" == "1" ]]; then draw; exit 0; fi
printf '\033[?25l'
while true; do draw; sleep "$INTERVAL"; done
