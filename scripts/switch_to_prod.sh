#!/bin/bash
# ============================================================
# easySVA 环境切换脚本 -> 切换至 [成品演示环境]
# 根目录: /opt/SVA, 前端: /var/www/SVA-web
# ============================================================
set -e

echo "=================================================="
echo "正在切换至 easySVA [成品演示环境] (v1.0 Frozen)..."
echo "=================================================="

# 1. 切换 Nginx 指向成品前端
if [ -f /etc/nginx/sites-enabled/default ]; then
    sudo sed -i 's|/var/www/SVA-web-dev/dist/|/var/www/SVA-web/dist/|g' /etc/nginx/sites-enabled/default
    sudo nginx -t && sudo nginx -s reload
    echo "[OK] Nginx 根目录已切换至 /var/www/SVA-web/dist/"
fi

# 2. 调用成品的自愈启动脚本
echo "[INFO] 正在拉起成品后台服务..."
sudo /opt/SVA/restart.sh

echo "=================================================="
echo ">>> 已成功切换至 [成品演示环境]！"
echo ">>> 运行目录: /opt/SVA"
echo ">>> Web 界面: http://127.0.0.1:80"
echo "=================================================="
