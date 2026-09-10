#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
easySVA 双国标流动态转桥与高可用自愈守护服务 (增强版)
- 实时同步 GbSipServer 注册状态，设备离线时不发起盲目点播，杜绝 404/409 刷屏
- 监控 GbSipServer 会话与 ZLMediaKit RTP 流
- 自动将设备1与设备2的国标 RTP 流实时转为零延迟规范 live FLV 流
- 自愈机制：当 ZLMediaKit 超时关闭 RTP 服务器或流中断时，自动清理并重启会话
- 彻底杜绝僵尸进程 (<defunct>) 与 'Already publishing' 冲突
"""
import time
import json
import urllib.request
import subprocess
import os
import signal
import sys

DEVICES = [
    {
        "name": "设备1-西门高精度球机",
        "device_id": "34020000001320000001",
        "channel_id": "34020000001320000002",
        "target_stream": "gb_34020000001320000002_0100000016"
    },
    {
        "name": "设备2-工位电脑摄像头",
        "device_id": "34020000001320000003",
        "channel_id": "34020000001320000004",
        "target_stream": "34020000001320000003"
    }
]

ZLM_API = "http://127.0.0.1:9992/index/api/getMediaList?secret=V3522025zlm0aA9ajn7UiOWi&app=rtp"
GBSIP_DEVICES = "http://127.0.0.1:18080/gb28181/api/devices"
GBSIP_SESSIONS = "http://127.0.0.1:18080/gb28181/api/sessions"
GBSIP_START = "http://127.0.0.1:18080/gb28181/api/live/start"
GBSIP_STOP = "http://127.0.0.1:18080/gb28181/api/live/stop"

# channel_id -> { "proc": Popen, "stream_id": str, "missing_count": int, "was_online": bool, "last_start_attempt": float }
bridges = {}


def log(msg: str):
    timestamp = time.strftime("%Y-%m-%d %H:%M:%S")
    print(f"[{timestamp}] [BridgeDaemon] {msg}", flush=True)


def get_online_devices():
    try:
        req = urllib.request.Request(GBSIP_DEVICES)
        with urllib.request.urlopen(req, timeout=3) as resp:
            data = json.loads(resp.read().decode('utf-8')).get('data', [])
            return {d.get('device_id') for d in data if d.get('online')}
    except Exception:
        return set()


def get_active_rtp_streams():
    try:
        req = urllib.request.Request(ZLM_API)
        with urllib.request.urlopen(req, timeout=3) as resp:
            data = json.loads(resp.read().decode('utf-8')).get('data', [])
            return {s.get('stream') for s in data if s.get('schema') == 'rtsp'}
    except Exception:
        return set()


def get_session_info(channel_id):
    try:
        req = urllib.request.Request(GBSIP_SESSIONS)
        with urllib.request.urlopen(req, timeout=3) as resp:
            data = json.loads(resp.read().decode('utf-8')).get('data', [])
            for s in data:
                if s.get('channel_id') == channel_id and s.get('state') == 'streaming':
                    return s.get('stream_id'), s.get('session_id')
    except Exception:
        pass
    return None, None


def trigger_live_start(device_id, channel_id):
    try:
        req = urllib.request.Request(
            GBSIP_START,
            data=json.dumps({"device_id": device_id, "channel_id": channel_id}).encode('utf-8'),
            headers={"Content-Type": "application/json"}
        )
        with urllib.request.urlopen(req, timeout=3) as resp:
            pass
        log(f"触发启动会话: device={device_id}, channel={channel_id}")
    except Exception as e:
        log(f"启动会话失败: {e}")


def trigger_live_stop(session_id):
    if not session_id:
        return
    try:
        req = urllib.request.Request(
            GBSIP_STOP,
            data=json.dumps({"session_id": session_id}).encode('utf-8'),
            headers={"Content-Type": "application/json"}
        )
        with urllib.request.urlopen(req, timeout=3) as resp:
            pass
        log(f"自愈机制：关闭失效会话 session_id={session_id}")
    except Exception as e:
        log(f"关闭会话失败: {e}")


def start_ffmpeg_bridge(stream_id, target_stream):
    cmd = [
        "ffmpeg",
        "-hide_banner", "-loglevel", "warning",
        "-allowed_media_types", "video",
        "-fflags", "+genpts",
        "-rtsp_transport", "tcp",
        "-timeout", "3000000",
        "-i", f"rtsp://127.0.0.1:9994/rtp/{stream_id}",
        "-c:v", "libx264", "-preset", "ultrafast", "-tune", "zerolatency",
        "-b:v", "1500k", "-maxrate", "2000k", "-bufsize", "3000k",
        "-g", "50", "-keyint_min", "25",
        "-fps_mode", "cfr", "-r", "25", "-an",
        "-f", "flv", f"rtmp://127.0.0.1:9995/live/{target_stream}"
    ]
    log_file = open(f"/opt/SVA-dev/gb_bridge_{target_stream}.log", "a")
    proc = subprocess.Popen(cmd, stdout=log_file, stderr=log_file)
    return proc


def kill_proc(proc):
    if not proc:
        return
    try:
        proc.terminate()
        proc.wait(timeout=1.5)
    except Exception:
        try:
            proc.kill()
            proc.wait(timeout=1.0)
        except Exception:
            pass


def cleanup(*args):
    log("收到退出信号，清理所有转推进程...")
    for b in bridges.values():
        kill_proc(b.get("proc"))
    sys.exit(0)


signal.signal(signal.SIGINT, cleanup)
signal.signal(signal.SIGTERM, cleanup)

log("启动 easySVA 高可用双国标转推守护服务...")

while True:
    try:
        online_devices = get_online_devices()
        active_rtp = get_active_rtp_streams()

        for dev in DEVICES:
            ch = dev["channel_id"]
            dev_id = dev["device_id"]
            target = dev["target_stream"]

            b = bridges.setdefault(ch, {
                "proc": None,
                "stream_id": None,
                "missing_count": 0,
                "was_online": None,
                "last_start_attempt": 0
            })
            proc = b["proc"]

            # 1. 检查并清理僵尸进程 (reap defunct)
            if proc is not None and proc.poll() is not None:
                proc.wait()  # 彻底回收，杜绝 <defunct>
                b["proc"] = None
                log(f"{dev['name']} 转推进程已退出 (code={proc.returncode})")

            # 2. 检查设备在线状态
            is_online = (dev_id in online_devices)
            if not is_online:
                if b.get("was_online") is not False:
                    log(f"{dev['name']} 处于离线/未注册状态，停止转推")
                    b["was_online"] = False
                if proc is not None:
                    kill_proc(proc)
                    b["proc"] = None
                b["stream_id"] = None
                b["missing_count"] = 0
                continue
            else:
                if b.get("was_online") is False:
                    log(f"{dev['name']} 恢复在线，开始调度会话")
                b["was_online"] = True

            # 3. 查询 GbSipServer 会话
            sid, sess_id = get_session_info(ch)
            if not sid:
                now = time.time()
                # 防抖：至少间隔 5 秒尝试一次，防止 409 Conflict 请求风暴
                if now - b.get("last_start_attempt", 0) >= 5.0:
                    b["last_start_attempt"] = now
                    trigger_live_start(dev_id, ch)
                continue

            # 4. 检查 RTP 流是否在 ZLM 中活跃
            if sid not in active_rtp:
                b["missing_count"] += 1
                # 若连续 3 次 (6秒) 查不到该 RTP 流，说明 ZLM 已超时回收该 RTP 端口
                if b["missing_count"] >= 3:
                    log(f"{dev['name']} RTP 流 {sid} 在 ZLM 中已离线超限，触发自愈重建会话...")
                    if proc is not None:
                        kill_proc(proc)
                        b["proc"] = None
                    trigger_live_stop(sess_id)
                    b["missing_count"] = 0
                continue
            else:
                b["missing_count"] = 0

            # 5. 若 stream_id 改变，重启对应转推进程
            if b["proc"] is not None and b["stream_id"] != sid:
                log(f"{dev['name']} stream_id 改变: {b['stream_id']} -> {sid}，重启转推进程...")
                kill_proc(b["proc"])
                b["proc"] = None

            # 6. 启动或维持转推进程
            if b["proc"] is None:
                log(f"启动转推: {dev['name']} rtp/{sid} -> live/{target}")
                time.sleep(0.3)  # 避开 RTMP publish 锁
                b["proc"] = start_ffmpeg_bridge(sid, target)
                b["stream_id"] = sid

    except Exception as e:
        log(f"主守护循环异常: {e}")

    time.sleep(2)
