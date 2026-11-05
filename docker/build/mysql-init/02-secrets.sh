#!/usr/bin/env bash
set -euo pipefail

# 首次建库后，从部署环境写入媒体服务密钥和管理员口令哈希。
: "${ZLM_SECRET:?必须设置 ZLM_SECRET}"
: "${ADMIN_PASSWORD_HASH:?必须设置 ADMIN_PASSWORD_HASH}"

zlm_hex=$(printf '%s' "$ZLM_SECRET" | od -An -tx1 | tr -d ' \n')
admin_hex=$(printf '%s' "$ADMIN_PASSWORD_HASH" | od -An -tx1 | tr -d ' \n')

MYSQL_PWD="$MARIADB_ROOT_PASSWORD" mariadb --protocol=socket --user=root --database=easySVA <<SQL
UPDATE zlm_server SET secret = CONVERT(UNHEX('$zlm_hex') USING utf8mb4) WHERE id = 1;
UPDATE sys_user SET password = CONVERT(UNHEX('$admin_hex') USING utf8mb4) WHERE user_id = 1;
SQL
