# easySVA Java 后端（RuoYi，JDK17 fat jar）
FROM eclipse-temurin:17-jre-jammy
ENV TZ=Asia/Shanghai JAVA_OPTS="-Xms256m -Xmx1024m -XX:MaxRAMPercentage=75.0"
WORKDIR /app

# 数据库 / Redis / 国标端口通过环境变量覆盖 jar 内 application(-druid).yml
ENV MYSQL_HOST=mysql \
    MYSQL_PORT=3306 \
    MYSQL_DB=easySVA \
    MYSQL_USER=root \
    MYSQL_PASSWORD=easySVA.EZ \
    REDIS_HOST=redis \
    REDIS_PORT=6379 \
    REDIS_DATABASE=3

COPY backend.jar ./app.jar
COPY --chmod=755 entrypoint.sh ./entrypoint.sh
EXPOSE 9114
ENTRYPOINT ["./entrypoint.sh"]
