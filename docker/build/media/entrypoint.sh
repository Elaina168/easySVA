#!/bin/bash
# media 容器入口：渲染配置并同时拉起 ZLMediaKit / GbSipServer / 国标转桥
set -e
cd /opt/media

# ---- 可通过环境变量覆盖的参数（带默认值）----
ZLM_SECRET="${ZLM_SECRET:-V3522025zlm0aA9ajn7UiOWi}"
PLATFORM_ID="${PLATFORM_ID:-34020000002000000001}"
REALM="${REALM:-3402000000}"
DEVICE_PASSWORD="${DEVICE_PASSWORD:-12345678}"
# SIP/SDP 中宣告给设备（模拟器）的地址：同一 docker 网络内用服务名 media
SIP_ADVERTISED_IP="${SIP_ADVERTISED_IP:-media}"

# ---- 1. 渲染 ZLM config.ini ----
cp zlm_config.ini config.ini
sed -i "s/^secret=.*/secret=${ZLM_SECRET}/" config.ini
sed -i "s/^check_nvidia_dev=.*/check_nvidia_dev=0/" config.ini
sed -i "s#^mp4_save_path=.*#mp4_save_path=/opt/media/upload/#" config.ini
# GB RTP 收流端口段收敛到 compose 映射范围
sed -i "s/^port_range=.*/port_range=30002-30100/" config.ini

# ---- 2. 渲染 GB28181 gb28181.ini ----
sed -i "s/^server_id=.*/server_id=${PLATFORM_ID}/" gb28181.ini
sed -i "s/^realm=.*/realm=${REALM}/" gb28181.ini
sed -i "s/^advertised_ip=.*/advertised_ip=${SIP_ADVERTISED_IP}/" gb28181.ini
# 管理 API 必须监听 0.0.0.0，backend 才能跨容器访问 18080
sed -i "s/^listen_ip=127.0.0.1/listen_ip=0.0.0.0/" gb28181.ini
sed -i "s/^device_password=.*/device_password=${DEVICE_PASSWORD}/" gb28181.ini
# GbSipServer 与 ZLM 同容器，zlm_api_url 保持 127.0.0.1 即可
export EASY_SVA_ZLM_API_SECRET="${ZLM_SECRET}"

# ---- 3. 退出时统一回收子进程 ----
PIDS=""
cleanup(){ echo "[media] 收到退出信号，停止子进程..."; kill $PIDS 2>/dev/null; exit 0; }
trap cleanup SIGTERM SIGINT

# ---- 4. 启动 ZLMediaKit ----
echo "[media] 启动 ZLMediaKit ..."
./MediaServer -d -c config.ini > zlm.log 2>&1 &
PIDS="$PIDS $!"

# 等待 ZLM HTTP 端口就绪
for i in $(seq 1 30); do
  if (echo > /dev/tcp/127.0.0.1/9992) 2>/dev/null; then echo "[media] ZLM 9992 就绪"; break; fi
  sleep 0.5
done

# ---- 5. 启动 GB28181 信令服务 ----
echo "[media] 启动 GbSipServer ..."
./GbSipServer > gbsip.log 2>&1 &
PIDS="$PIDS $!"

# ---- 6. 启动国标转桥（可选关闭：ENABLE_BRIDGE=0）----
if [ "${ENABLE_BRIDGE:-1}" = "1" ]; then
  echo "[media] 启动国标转桥 ..."
  ./gb_bridge.sh > bridge.log 2>&1 &
  PIDS="$PIDS $!"
fi

echo "[media] 全部组件已启动 (ZLM/GbSip/Bridge)"
# 任一子进程退出则整体退出，交给 compose 重启策略
wait -n $PIDS
echo "[media] 有子进程退出，容器结束"
exit 1
