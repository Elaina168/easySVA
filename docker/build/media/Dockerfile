# easySVA 媒体容器：ZLMediaKit + GB28181 信令 + 国标转桥（三者按原项目同机部署）
FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive TZ=Asia/Shanghai
RUN apt-get update && apt-get install -y --no-install-recommends \
        libssl3 ffmpeg python3 ca-certificates tzdata procps bash \
    && ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt/media
# ZLMediaKit 运行文件
COPY --chmod=755 MediaServer ./MediaServer
COPY config.ini ./zlm_config.ini
COPY default.pem ./default.pem
COPY www ./www
# GB28181 信令服务运行文件
COPY --chmod=755 GbSipServer ./GbSipServer
COPY gb28181.ini ./gb28181.ini
# 启动与转桥脚本
COPY --chmod=755 entrypoint.sh ./entrypoint.sh
COPY --chmod=755 gb_bridge.sh ./gb_bridge.sh

RUN mkdir -p ffmpeg www/snap www/live upload/alarm upload/storage

# HTTP-FLV/WS-FLV 9992 / RTSP 9994 / RTMP 9995 / GB管理API 18080 / SIP 5060(UDP+TCP)
EXPOSE 9992 9994 9995 18080 5060/tcp 5060/udp
ENTRYPOINT ["./entrypoint.sh"]
