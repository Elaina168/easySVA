-- ============================================================
-- easySVA 容器初始化修正脚本（在 01-schema.sql 导入后自动执行）
-- 作用：把服务地址改成 docker 网络可达地址，并清空测试/日志数据
-- ============================================================
USE easySVA;
SET FOREIGN_KEY_CHECKS=0;

-- 1) 媒体服务器 + 国标服务（后端代码复用同一 host 访问 9992 与 18080）
--    host.docker.internal 经“宿主端口映射”回到 media 容器，浏览器拉流也走它
UPDATE zlm_server
   SET host = 'host.docker.internal',
       api_port = 9992, media_http_port = 9992, media_rtsp_port = 9994
 WHERE id = 1;

-- 2) 分析引擎仅被后端服务端调用，直接用容器内部服务名 analyzer
UPDATE sva_server SET host = 'analyzer', analyzer_port = 9993 WHERE id = 1;

-- 3) 流地址按“谁来访问”区分（关键）：
--    stream_url / push_stream_url 是 analyzer 容器拉流、回推标注流到 ZLM，走 docker 内部服务名 media；
--    algorithm_stream_url 是浏览器播放带算法框的画面，走 host.docker.internal（经宿主端口映射）。
UPDATE deployment_task SET
  stream_url        = REPLACE(REPLACE(stream_url,'127.0.0.1','media'),'localhost','media'),
  push_stream_url   = REPLACE(REPLACE(push_stream_url,'127.0.0.1','media'),'localhost','media'),
  algorithm_stream_url = REPLACE(REPLACE(algorithm_stream_url,'127.0.0.1','host.docker.internal'),'localhost','host.docker.internal');
-- 布控先复位为停止，避免服务未齐就拉流；登录后在页面点“启动”即可演示
UPDATE deployment_task SET status = 'STOPPED', start_time = NULL;
UPDATE h_device SET
  device_name = '模拟摄像头-睡岗监控',
  is_online = '0', monitor_status = 'STOPPED',
  play_url = REPLACE(REPLACE(play_url,'127.0.0.1','host.docker.internal'),'localhost','host.docker.internal');

-- 4) 清空历史测试告警与运行日志（保留菜单/用户/算法/设备/布控配置）
DELETE FROM h_waring;
DELETE FROM h_handle;
DELETE FROM sys_logininfor;
DELETE FROM sys_oper_log;
DELETE FROM sys_job_log;
DELETE FROM sys_user_online;

SET FOREIGN_KEY_CHECKS=1;
