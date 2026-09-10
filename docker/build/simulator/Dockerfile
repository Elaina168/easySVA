# GB28181 国标模拟摄像头（可选演示容器，profile=demo）
FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive TZ=Asia/Shanghai
RUN apt-get update && apt-get install -y --no-install-recommends \
        python3 ffmpeg ca-certificates tzdata bash \
    && ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY gb28181_device_simulator.py ./
COPY sleep_source.mp4 ./
COPY --chmod=755 entrypoint.sh ./
EXPOSE 15060
ENTRYPOINT ["./entrypoint.sh"]
