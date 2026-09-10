#!/bin/bash
# easySVA 双国标流动态转换桥守护脚本
# 自动启动 gb_bridge_daemon.py

LOG="/opt/SVA-dev/gb_bridge.log"
echo "[$(date '+%F %T')] 启动双国标转桥守护进程..." >> "$LOG"

exec python3 /opt/SVA-dev/gb_bridge_daemon.py >> "$LOG" 2>&1
