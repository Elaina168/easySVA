-- =============================================================
-- easySVA 任务四：h_device 表结构增量变更（GB28181 支持）
-- 分支：feature/java-backend
-- 目标库：easySVA（MySQL）
-- 说明：
--   1) 本脚本为增量迁移，在新环境部署时执行一次即可；
--   2) 若开发库已手工添加过 1~3 步字段，可只执行第 4 步索引；
--   3) MySQL 8.0 不支持 ADD COLUMN IF NOT EXISTS，重复执行第 1~3 步
--      会报 "Duplicate column name"，属预期行为，忽略后跳过即可。
-- 执行：mysql -uroot -p easySVA < alter_h_device.sql
-- =============================================================

-- 1. 设备类型（rtsp 普通流 / gb28181 国标），默认 rtsp 兼容存量设备
ALTER TABLE h_device
    ADD COLUMN device_type VARCHAR(20) NOT NULL DEFAULT 'rtsp'
    COMMENT '设备类型：rtsp-普通流/gb28181-国标' AFTER stream_source_type;

-- 2. GB28181 国标设备 ID（20 位国标编码，如 34020000001320000001）
ALTER TABLE h_device
    ADD COLUMN gb_device_id VARCHAR(64) DEFAULT NULL
    COMMENT 'GB28181 国标设备ID' AFTER device_type;

-- 3. GB28181 上级平台 ID
ALTER TABLE h_device
    ADD COLUMN gb_platform_id VARCHAR(64) DEFAULT NULL
    COMMENT 'GB28181 上级平台ID' AFTER gb_device_id;

-- 4. gb_device_id 唯一索引：防止并发同步重复插入
--    （NULL 不参与唯一约束，不影响存量 RTSP 设备）
CREATE UNIQUE INDEX uk_gb_device_id ON h_device (gb_device_id);
