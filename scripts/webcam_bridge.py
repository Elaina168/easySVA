#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
easySVA 电脑摄像头 WebSocket 接入与推流桥接服务 (增强高可用防卡顿版)
端口: 18090 (WebSocket)
功能: 接收浏览器 MediaRecorder 发送的 WebM 视频块，即时管道注入 FFmpeg 并推流至 ZLM live/webcam (RTMP/RTSP)
在无真实摄像头推流时，自动维持待机画面，保证 rtsp://127.0.0.1:9994/live/webcam 始终在线
防卡顿增强:
1. 保证单客户端互斥：新连接到达时，优雅关闭旧连接并清理旧 FFmpeg，杜绝无头帧写入导致管道崩溃。
2. 管道崩溃/中断时，0.3s 内快速自愈回退到待机信号，防止国标 RTP 下游断流超时。
3. 优化 FFmpeg 转码参数：-fflags +nobuffer+genpts -tune zerolatency，杜绝管道积压。
"""

import asyncio
import os
import signal
import subprocess
import sys
import time
import websockets

PORT = 18090
RTMP_TARGET = "rtmp://127.0.0.1:9995/live/webcam"
LOG_FILE = "/opt/SVA-dev/webcam_bridge.log"

active_live_proc = None
standby_proc = None
active_ws = None
stream_lock = asyncio.Lock()


def log(msg: str):
    timestamp = time.strftime("%Y-%m-%d %H:%M:%S")
    line = f"[{timestamp}] [WebcamBridge] {msg}\n"
    sys.stdout.write(line)
    sys.stdout.flush()
    try:
        with open(LOG_FILE, "a", encoding="utf-8") as f:
            f.write(line)
    except Exception:
        pass


def kill_process(proc):
    if not proc:
        return
    try:
        if proc.poll() is None:
            proc.terminate()
            proc.wait(timeout=1.0)
    except Exception:
        try:
            proc.kill()
            proc.wait(timeout=0.5)
        except Exception:
            pass


def start_standby():
    global standby_proc
    if standby_proc and standby_proc.poll() is None:
        return
    log("启动待机推流守护 (testsrc2 -> live/webcam)...")
    cmd = [
        "ffmpeg", "-hide_banner", "-loglevel", "error",
        "-re", "-f", "lavfi", "-i", "testsrc2=size=1280x720:rate=25",
        "-c:v", "libx264", "-preset", "ultrafast", "-tune", "zerolatency",
        "-pix_fmt", "yuv420p",
        "-r", "25", "-g", "25", "-an", "-f", "flv", RTMP_TARGET
    ]
    standby_proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def stop_standby():
    global standby_proc
    if standby_proc and standby_proc.poll() is None:
        log("停止待机推流...")
        kill_process(standby_proc)
    standby_proc = None


async def handler(websocket):
    global active_live_proc, standby_proc, active_ws
    remote_addr = websocket.remote_address
    log(f"收到电脑摄像头连接: {remote_addr}")

    async with stream_lock:
        # 若已有旧连接，优雅断开并等待旧流清理
        if active_ws and active_ws != websocket:
            log(f"检测到新客户端接入，关闭前一个摄像头连接...")
            try:
                await active_ws.close(1000, "New client connected")
            except Exception:
                pass
            await asyncio.sleep(0.2)

        active_ws = websocket

        # 停待机推流
        stop_standby()

        # 确保旧 live 进程完全退出并释放 RTMP 发布锁
        if active_live_proc and active_live_proc.poll() is None:
            try:
                active_live_proc.stdin.close()
            except Exception:
                pass
            kill_process(active_live_proc)
            active_live_proc = None
            await asyncio.sleep(0.3)

        # 启动接收真实摄像头 WebM 块的 FFmpeg
        log("启动实时摄像头转码推流管线 (WebM pipe:0 -> libx264 -> live/webcam)...")
        cmd = [
            "ffmpeg", "-hide_banner", "-loglevel", "warning",
            "-fflags", "+nobuffer+genpts", "-flags", "low_delay",
            "-f", "webm", "-i", "pipe:0",
            "-c:v", "libx264", "-preset", "ultrafast", "-tune", "zerolatency",
            "-pix_fmt", "yuv420p",
            "-r", "25", "-g", "25", "-an", "-f", "flv", RTMP_TARGET
        ]
        active_live_proc = subprocess.Popen(
            cmd, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
        )

    chunk_count = 0
    pipe_error = False

    try:
        async for message in websocket:
            # 仅允许当前处于活跃的 websocket 写入
            if active_ws != websocket:
                break

            if isinstance(message, bytes):
                if active_live_proc and active_live_proc.poll() is None:
                    try:
                        active_live_proc.stdin.write(message)
                        active_live_proc.stdin.flush()
                        chunk_count += 1
                        if chunk_count % 100 == 0:
                            log(f"已流式注入 {chunk_count} 个摄像头视频切片...")
                    except (BrokenPipeError, IOError):
                        log("FFmpeg 管道异常 (Broken pipe)，退出当前流注入")
                        pipe_error = True
                        break
                else:
                    log("FFmpeg 进程已停止运行，退出接收循环")
                    pipe_error = True
                    break
            elif isinstance(message, str) and message == "ping":
                await websocket.send("pong")
    except websockets.exceptions.ConnectionClosed:
        log(f"摄像头连接已断开: {remote_addr}")
    except Exception as e:
        log(f"摄像头流传输异常: {e}")
    finally:
        async with stream_lock:
            if active_ws == websocket:
                log(f"摄像头推流结束，共注入 {chunk_count} 个分片，切回待机模式")
                active_ws = None
                if active_live_proc and active_live_proc.poll() is None:
                    try:
                        active_live_proc.stdin.close()
                    except Exception:
                        pass
                    kill_process(active_live_proc)
                active_live_proc = None
                await asyncio.sleep(0.3)
                start_standby()


async def main():
    start_standby()
    log(f"WebSocket 摄像头中继服务正在监听 0.0.0.0:{PORT}...")
    async with websockets.serve(handler, "0.0.0.0", PORT, max_size=20 * 1024 * 1024):
        await asyncio.Future()  # 永不退出


if __name__ == "__main__":
    def sig_handler(_signum, _frame):
        log("收到退出信号，清理推流进程...")
        stop_standby()
        if active_live_proc:
            kill_process(active_live_proc)
        sys.exit(0)

    signal.signal(signal.SIGINT, sig_handler)
    signal.signal(signal.SIGTERM, sig_handler)

    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
