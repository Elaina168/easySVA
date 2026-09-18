#!/bin/bash
# Analyzer 入口：按容器环境渲染 config.json 后启动
set -e
cd /app

BACKEND_HOST="${BACKEND_HOST:-backend}"
ZLM_SECRET="${ZLM_SECRET:-__INJECTED_AT_RUNTIME__}"
# 自身绑定地址必须 0.0.0.0，backend 才能跨容器访问 9993
BIND_HOST="${BIND_HOST:-0.0.0.0}"

sed -i "s#\"host\":.*#\"host\": \"${BIND_HOST}\",#" config.json
sed -i "s#\"mediaSecret\":.*#\"mediaSecret\": \"${ZLM_SECRET}\",#" config.json
sed -i "s#\"uploadDir\":.*#\"uploadDir\": \"/app/upload\",#" config.json
sed -i "s#\"modelDir\":.*#\"modelDir\": \"/app/models\",#" config.json
sed -i "s#\"saveAlarmUrl\":.*#\"saveAlarmUrl\": \"http://${BACKEND_HOST}:9114/waring/waring/addFromSvaSimple\",#" config.json
sed -i "s#\"detectEventUrl\":.*#\"detectEventUrl\": \"ws://${BACKEND_HOST}:9114/websocket/sva/noop\"#" config.json

echo "[analyzer] 配置渲染完成"
exec ./bin/Analyzer -f config.json
