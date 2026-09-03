-- =============================================================
-- easySVA 任务四：h_device 表结构增量变更（GB28181 支持）
-- 分支：feature/java-backend
-- 目标库：easySVA（MariaDB 10.6+）
-- 说明：
--   1) 本脚本为幂等增量迁移，可重复执行，不会重复加列/索引；
--   2) 依赖 MariaDB 10.0.2+ 的 ADD COLUMN IF NOT EXISTS
--      与 CREATE [UNIQUE] INDEX IF NOT EXISTS 语法；
--   3) 第 4 步唯一索引前建议先检查存量重复 gb_device_id，
--      若存在重复需先清理后再执行，否则唯一索引会创建失败。
-- 执行：mysql -uroot -p easySVA < alter_h_device.sql
-- =============================================================

-- 1. 设备类型（rtsp 普通流 / gb28181 国标），默认 rtsp 兼容存量设备
ALTER TABLE h_device
    ADD COLUMN IF NOT EXISTS device_type VARCHAR(20) NOT NULL DEFAULT 'rtsp'
    COMMENT '设备类型：rtsp-普通流/gb28181-国标' AFTER stream_source_type;

-- 2. GB28181 国标设备 ID（20 位国标编码，如 34020000001320000001）
ALTER TABLE h_device
    ADD COLUMN IF NOT EXISTS gb_device_id VARCHAR(64) DEFAULT NULL
    COMMENT 'GB28181 国标设备ID' AFTER device_type;

-- 3. GB28181 上级平台 ID
ALTER TABLE h_device
    ADD COLUMN IF NOT EXISTS gb_platform_id VARCHAR(64) DEFAULT NULL
    COMMENT 'GB28181 上级平台ID' AFTER gb_device_id;

-- 4. gb_device_id 唯一索引：防止并发同步重复插入
--    （NULL 不参与唯一约束，不影响存量 RTSP 设备）
--    前置检查：若存在重复的非空 gb_device_id，先执行下方查询定位并清理后重跑：
--    SELECT gb_device_id, COUNT(*) FROM h_device
--    WHERE gb_device_id IS NOT NULL GROUP BY gb_device_id HAVING COUNT(*) > 1;
CREATE UNIQUE INDEX IF NOT EXISTS uk_gb_device_id ON h_device (gb_device_id);
