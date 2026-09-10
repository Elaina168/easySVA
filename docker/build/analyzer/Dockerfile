# easySVA 视频分析引擎（C++ Analyzer，CPU 推理，搬运已编译二进制+依赖闭包）
FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive TZ=Asia/Shanghai
RUN sed -i 's/^# deb/deb/g' /etc/apt/sources.list 2>/dev/null || true \
    && apt-get update && apt-get install -y --no-install-recommends \
        libssl3 libevent-2.1-7 libjsoncpp25 \
        libjpeg-turbo8 libpng16-16 libtiff5 libwebp7 libjbig0 libdeflate0 \
        libx264-163 libx265-199 libmp3lame0 libopus0 \
        libbrotli1 libzstd1 liblzma5 libnuma1 zlib1g \
        ca-certificates tzdata bash \
    && ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY bin ./bin
COPY lib ./lib
COPY models ./models
COPY config.json ./config.json
COPY --chmod=755 entrypoint.sh ./entrypoint.sh
RUN mkdir -p /app/upload

ENV LD_LIBRARY_PATH=/app/lib
# analyzerPort 9993（backend 调用），adminPort 9991
EXPOSE 9993 9991
ENTRYPOINT ["./entrypoint.sh"]
