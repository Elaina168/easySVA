#!/bin/bash
# ============================================================
# SVA 本地服务监控面板（只看本机 SVA 相关服务，不含国标设备状态）
# 每 INTERVAL 秒刷新；Ctrl+C 退出，关闭窗口不影响后台服务。
#   bash /opt/SVA-dev/sva_monitor.sh
#   ONESHOT=1 bash /opt/SVA-dev/sva_monitor.sh   # 单帧自检
# 国标设备(SIP注册/心跳/信令/PTZ)请看 gb_monitor.sh
# ============================================================

INTERVAL=2
ZLM_SECRET="V3522025zlm0aA9ajn7UiOWi"
DB_USER="root"; DB_PASS="easySVA.EZ"; DB_NAME="easySVA"

R=$'\033[31m'; G=$'\033[32m'; Y=$'\033[33m'; C=$'\033[36m'; B=$'\033[1m'; D=$'\033[2m'; N=$'\033[0m'

mysql_q(){ mysql -u"$DB_USER" -p"$DB_PASS" "$DB_NAME" -N -e "$1" 2>/dev/null; }
http_code(){ curl -s -o /dev/null -w '%{http_code}' --max-time 1 "$1" 2>/dev/null; }
proc_pid(){ # 精确取进程PID(可多个)
  local name="$1" pids=""
  for p in $(pgrep -x "$name" 2>/dev/null); do pids="$pids $p"; done
  echo "$pids"
}

trap 'printf "\033[?25h\033[0m"; exit 0' INT TERM EXIT

draw(){
  local NOW=$(date '+%F %T')
  local down="" ok=0 total=0 names=() marks=()

  add_http(){ local code; code=$(http_code "$2"); total=$((total+1))
    if [[ "$code" == "200" ]]; then names+=("$1"); marks+=("${G}● OK${N}"); ok=$((ok+1))
    else names+=("$1"); marks+=("${R}● DOWN${N} ${D}(${code:-无响应})${N}"); down="$down $1"; fi; }
  add_db(){ total=$((total+1))
    if mysql_q "SELECT 1;" >/dev/null 2>&1; then names+=("$1"); marks+=("${G}● OK${N}"); ok=$((ok+1))
    else names+=("$1"); marks+=("${R}● DOWN${N}"); down="$down $1"; fi; }

  add_http "ZLMediaKit:9992" "http://127.0.0.1:9992/index/api/getServerConfig?secret=$ZLM_SECRET"
  add_http "GbSip信令服务:18080" "http://127.0.0.1:18080/gb28181/api/devices"
  add_http "Analyzer分析:9993"  "http://127.0.0.1:9993/"
  add_http "Java后端:9114"  "http://127.0.0.1:9114/login"
  add_http "前端nginx:80"   "http://127.0.0.1/"
  add_db   "MariaDB:3306"

  # ---- 后台进程(本地进程是否存活+PID) ----
  local p_zlm p_gbsip p_ana p_java n_ff
  p_zlm=$(proc_pid MediaServer); p_gbsip=$(proc_pid GbSipServer); p_ana=$(proc_pid Analyzer); p_java=$(pgrep -f backend.jar 2>/dev/null | tr '\n' ' ')
  n_ff=$(pgrep -x ffmpeg 2>/dev/null | wc -l)
  proc_line(){ if [[ -n "$2" ]]; then printf "  %-14s ${G}运行中${N} PID:%s\n" "$1" "$2"; else printf "  %-14s ${R}未运行${N}\n" "$1"; fi; }

  # ---- 布控 ----
  local deploy; deploy=$(mysql_q "SELECT GROUP_CONCAT(CONCAT(task_name,' [',status,']') SEPARATOR '; ') FROM deployment_task;" 2>/dev/null)
  [[ -z "$deploy" ]] && deploy="${D}无布控任务${N}"

  # ---- 视频流 ----
  local streaminfo; streaminfo=$(curl -s --max-time 1 "http://127.0.0.1:9992/index/api/getMediaList?secret=$ZLM_SECRET" 2>/dev/null | python3 -c "
import sys,json
try:
    d=json.load(sys.stdin).get('data') or []
    s=sorted(set('%s/%s'%(x.get('app'),x.get('stream')) for x in d))
    print('%d 路: %s'%(len(s), '  '.join(s)) if s else '0 路')
except Exception: print('查询失败')" 2>/dev/null)

  # ---- 告警 ----
  local acnt alarm; acnt=$(mysql_q "SELECT COUNT(*) FROM h_waring;" 2>/dev/null); acnt=${acnt:-0}
  alarm=$(mysql_q "SELECT CONCAT('#',w_id,' ',alarm_type_name,' ',alarm_time) FROM h_waring ORDER BY w_id DESC LIMIT 1;" 2>/dev/null)
  [[ -z "$alarm" ]] && alarm="${D}暂无告警${N}"

  # ---- 系统资源 ----
  local load mem
  load=$(awk '{print $1", "$2", "$3}' /proc/loadavg)
  mem=$(free -m | awk '/Mem:/{printf "已用 %dM / %dM (%.0f%%)",$3,$2,$3/$2*100}')

  printf '\033[2J\033[H'
  printf "${C}${B}════════════ SVA 本地服务监控（不含国标设备）%s 每%ds刷新 ════════════${N}\n" "$NOW" "$INTERVAL"
  printf "${B}【服务接口健康】${N}\n"
  local i
  for ((i=0;i<${#names[@]};i+=2)); do
    if [[ $((i+1)) -lt ${#names[@]} ]]; then
      printf "  %-20s %-26b  %-20s %b\n" "${names[$i]}" "${marks[$i]}" "${names[$((i+1))]}" "${marks[$((i+1))]}"
    else printf "  %-20s %b\n" "${names[$i]}" "${marks[$i]}"; fi
  done

  printf "${B}【后台进程】${N}\n"
  proc_line "MediaServer" "$p_zlm"
  proc_line "GbSipServer" "$p_gbsip"
  proc_line "Analyzer"    "$p_ana"
  proc_line "Java后端"    "$p_java"
  if [[ "$n_ff" -gt 0 ]]; then printf "  %-14s ${G}运行中${N} ffmpeg进程 %s 个(推流/转桥)\n" "FFmpeg" "$n_ff"
  else printf "  %-14s ${Y}无 ffmpeg 进程${N}\n" "FFmpeg"; fi

  printf "${B}【布控】${N} %s\n" "$deploy"
  printf "${B}【视频流】${N} %s\n" "$streaminfo"
  printf "${B}【告警】${N} 累计 %s 条，最新：%s\n" "$acnt" "$alarm"
  printf "${B}【资源】${N} 负载(1/5/15min): %s    内存: %s    CPU核数: %s\n" "$load" "$mem" "$(nproc)"
  printf "${D}──────────────────────────────────────────────────────────────────${N}\n"
  if [[ -z "$down" ]]; then printf "${G}${B}状态：● SVA 本地服务全部正常 (%d/%d)${N}\n" "$ok" "$total"
  else printf "${R}${B}状态：✗ 异常 ->%s${N}\n${Y}  恢复命令: bash /opt/SVA-dev/restart.sh${N}\n" "$down"; fi
  printf "${D}Ctrl+C 关闭（只读，不影响服务）｜国标设备请看 gb_monitor.sh${N}\n"
}

if [[ "${ONESHOT:-0}" == "1" ]]; then draw; exit 0; fi
printf '\033[?25l'
while true; do draw; sleep "$INTERVAL"; done
