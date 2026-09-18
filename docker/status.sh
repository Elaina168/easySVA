#!/usr/bin/env bash
# 查看服务状态和关键服务最近日志。
set -euo pipefail
cd "$(dirname "$0")"

docker compose -f compose.json ps
docker compose -f compose.json logs --tail=30 backend media analyzer web
