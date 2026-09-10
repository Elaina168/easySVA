#!/bin/bash
# ============================================================
# easySVA 环境切换脚本 -> 切换至 [二次开发环境]
# 根目录: /opt/SVA-dev, 前端: /var/www/SVA-web-dev
# ============================================================
set -e

echo "=================================================="
echo "正在切换至 easySVA [二次开发环境] (v2.0 Dev)..."
echo "=================================================="

# 1. 切换 Nginx 指向开发前端
if [ -f /etc/nginx/sites-enabled/default ]; then
    sudo sed -i 's|/var/www/SVA-web/dist/|/var/www/SVA-web-dev/dist/|g' /etc/nginx/sites-enabled/default
    sudo nginx -t && sudo nginx -s reload
    echo "[OK] Nginx 根目录已切换至 /var/www/SVA-web-dev/dist/"
fi

# 2. 调用开发副本的自愈启动脚本
echo "[INFO] 正在拉起开发后台服务..."
sudo /opt/SVA-dev/restart.sh

echo "=================================================="
echo ">>> 已成功切换至 [二次开发环境]！"
echo ">>> 开发目录: /opt/SVA-dev"
echo ">>> 前端开发: /var/www/SVA-web-dev"
echo ">>> Web 界面: http://127.0.0.1:80"
echo "=================================================="
