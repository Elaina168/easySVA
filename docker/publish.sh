#!/usr/bin/env bash
# 完成本机验收后，由用户执行此脚本开放 Web 入口。
set -euo pipefail
cd "$(dirname "$0")"
curl --fail --silent http://127.0.0.1/ > /dev/null
python3 - <<'PY'
from pathlib import Path
p = Path('.env')
s = p.read_text(encoding='utf-8')
assert 'WEB_BIND=127.0.0.1' in s or 'WEB_BIND=0.0.0.0' in s
p.write_text(s.replace('WEB_BIND=127.0.0.1','WEB_BIND=0.0.0.0'), encoding='utf-8')
PY
docker compose -f compose.json up -d web
echo 'Web 入口已配置为公网监听；仍需从外部浏览器验收。'
