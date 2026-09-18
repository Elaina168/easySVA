#!/usr/bin/env bash
# 一键构建并启动；重复执行时复用 Docker 构建缓存和已有数据卷。
set -euo pipefail
cd "$(dirname "$0")"

command -v docker >/dev/null || { echo '未找到 docker 命令' >&2; exit 1; }
docker compose version >/dev/null
sha256sum -c SHA256SUMS >/dev/null
docker compose -f compose.json config --quiet
docker compose -f compose.json up -d --build --remove-orphans

echo '等待 Web 服务就绪……'
for _ in $(seq 1 90); do
    if curl --fail --silent --max-time 2 http://127.0.0.1/ >/dev/null 2>&1; then
        docker compose -f compose.json ps
        echo 'easySVA 已启动。访问地址见 .env 中的 PUBLIC_BASE_URL。'
        exit 0
    fi
    sleep 2
done

echo '启动超时，输出最近日志：' >&2
docker compose -f compose.json ps >&2
docker compose -f compose.json logs --tail=80 backend media analyzer web >&2
exit 1
