#!/usr/bin/env bash
# 一键安全停止；保留数据库、告警图片、录像、镜像和数据卷。
set -euo pipefail
cd "$(dirname "$0")"

command -v docker >/dev/null || { echo '未找到 docker 命令' >&2; exit 1; }
docker compose version >/dev/null
docker compose -f compose.json stop --timeout 30
docker compose -f compose.json ps
echo 'easySVA 已停止，数据已保留。再次运行 start.sh 即可恢复。'
