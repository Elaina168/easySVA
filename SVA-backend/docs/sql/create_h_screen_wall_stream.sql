-- =============================================================
-- easySVA: h_screen_wall_stream 表结构定义与实时监控菜单迁移
-- 目标库：easySVA（MariaDB 10.6+ / MySQL 8.0+）
-- 说明：
--   1) 修复首页大屏实时监控报错：Table 'easySVA.h_screen_wall_stream' doesn't exist；
--   2) 将菜单 2002 名称更新为「实时监控」，支持从左侧菜单直接进入分屏实时监控；
--   3) 包含幂等验收流与初始化配置。
-- 执行：mysql -uroot -p easySVA < create_h_screen_wall_stream.sql
-- =============================================================

-- 1. 创建监控墙流配置表
CREATE TABLE IF NOT EXISTS `h_screen_wall_stream` (
  `id` bigint(20) NOT NULL AUTO_INCREMENT COMMENT '主键ID',
  `wall_code` varchar(64) NOT NULL DEFAULT 'main' COMMENT '监控墙编码',
  `source_type` varchar(32) NOT NULL DEFAULT 'task' COMMENT '源类型(task/realtime)',
  `source_id` varchar(64) NOT NULL COMMENT '源业务ID',
  `device_id` varchar(64) DEFAULT NULL COMMENT '设备ID/编码',
  `play_url` varchar(1024) DEFAULT NULL COMMENT '播放地址',
  `title` varchar(128) DEFAULT NULL COMMENT '标题/名称',
  `slot_index` int(11) DEFAULT NULL COMMENT '分屏槽位索引',
  `enabled` tinyint(4) NOT NULL DEFAULT 1 COMMENT '是否启用(1启用 0禁用)',
  `create_time` datetime DEFAULT NULL COMMENT '创建时间',
  `update_time` datetime DEFAULT NULL COMMENT '更新时间',
  PRIMARY KEY (`id`),
  KEY `idx_wall_code` (`wall_code`),
  KEY `idx_device_id` (`device_id`),
  KEY `idx_source_id` (`source_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='监控墙流配置表';

-- 2. 菜单更新：将菜单 2002 命名为「实时监控」
UPDATE sys_menu
SET menu_name = '实时监控',
    remark = '设备视频实时监控与多分屏预览',
    update_by = 'admin',
    update_time = NOW()
WHERE menu_id = 2002;

-- 3. 验收初始数据（幂等）
INSERT INTO h_device
  (ape_id, name, stream_source_type, direct_source_url, play_url, resource_type, sub_type,
   org_index, org_name, place_code, place, is_online, zlm_server_id, sva_server_id,
   monitor_status, create_time, update_time)
VALUES
  ('acceptance-camera', '验收测试摄像头', 'DIRECT', 'rtsp://127.0.0.1:9994/live/acceptance', 'http://127.0.0.1:9992/live/acceptance.live.flv', 'VIDEO', 'RTSP',
   '10', '验收组', 'acceptance', '本地测试机房', '1', 1, 1,
   'RUNNING', NOW(), NOW())
ON DUPLICATE KEY UPDATE
  name = VALUES(name), direct_source_url = VALUES(direct_source_url), play_url = VALUES(play_url),
  is_online = '1', monitor_status = 'RUNNING', update_time = NOW();

INSERT INTO deployment_task
  (deployment_id, task_name, device_id, algorithm_code, algorithm_name, target_code,
   push_enabled, frontend_overlay_enabled, record_engine, alarm_interval_sec,
   geometry_config, stream_url, status, create_time, update_time)
VALUES
  ('acceptance-camera-yolo', '验收RTSP-YOLO布控', 'acceptance-camera', 'on_yolo11n_80', 'YOLO11n', 'person',
   0, 1, 'A-SERVER', 3,
   '{"regions":[{"id":"region_primary","name":"主区域","type":"polygon","primary":true,"closed":true,"points":[{"x":0,"y":0},{"x":1,"y":0},{"x":1,"y":1},{"x":0,"y":1}]}],"lines":[],"behaviorRules":[]}',
   'rtsp://127.0.0.1:9994/live/acceptance', 'RUNNING', NOW(), NOW())
ON DUPLICATE KEY UPDATE
  task_name = VALUES(task_name), device_id = VALUES(device_id), algorithm_code = VALUES(algorithm_code),
  algorithm_name = VALUES(algorithm_name), stream_url = VALUES(stream_url), status = 'RUNNING', update_time = NOW();

INSERT INTO h_screen_wall_stream
  (wall_code, source_type, source_id, device_id, play_url, title, slot_index, enabled, create_time, update_time)
SELECT 'main', 'realtime', 'acceptance-camera', 'acceptance-camera', 'http://127.0.0.1:9992/live/acceptance.live.flv', '验收测试摄像头', 0, 1, NOW(), NOW()
FROM DUAL
WHERE NOT EXISTS (SELECT 1 FROM h_screen_wall_stream WHERE wall_code = 'main' AND slot_index = 0);
