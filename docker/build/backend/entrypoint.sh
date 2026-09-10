#!/bin/bash
# backend 入口：等待 mysql / redis 就绪后启动 Spring Boot
set -e
wait_tcp(){ # host port desc
  for i in $(seq 1 60); do
    if (echo > /dev/tcp/$1/$2) 2>/dev/null; then echo "[backend] $3 就绪"; return 0; fi
    echo "[backend] 等待 $3 ($1:$2) ..."; sleep 2
  done
  echo "[backend] 等待 $3 超时，继续尝试启动"; return 0
}
wait_tcp "$MYSQL_HOST" "$MYSQL_PORT" "MySQL"
wait_tcp "$REDIS_HOST" "$REDIS_PORT" "Redis"

export SPRING_DATASOURCE_DRUID_MASTER_URL="jdbc:mysql://${MYSQL_HOST}:${MYSQL_PORT}/${MYSQL_DB}?useUnicode=true&characterEncoding=utf8&zeroDateTimeBehavior=convertToNull&useSSL=false&serverTimezone=GMT%2B8&allowPublicKeyRetrieval=true"
export SPRING_DATASOURCE_DRUID_MASTER_USERNAME="$MYSQL_USER"
export SPRING_DATASOURCE_DRUID_MASTER_PASSWORD="$MYSQL_PASSWORD"
export SPRING_DATA_REDIS_HOST="$REDIS_HOST"
export SPRING_DATA_REDIS_PORT="$REDIS_PORT"
export SPRING_DATA_REDIS_DATABASE="$REDIS_DATABASE"

exec java $JAVA_OPTS -jar app.jar
