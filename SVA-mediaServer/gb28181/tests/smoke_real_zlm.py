#!/usr/bin/env python3
"""Run the complete GB28181 path against a real ZLMediaKit process."""

from __future__ import annotations

import argparse
import configparser
import json
import socket
import subprocess
import sys
import tempfile
import time
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

import smoke_device_simulator as harness


def zlm_request(port: int, path: str, parameters: Dict[str, str]) -> Dict[str, Any]:
    body = urllib.parse.urlencode(parameters).encode("utf-8")
    request = urllib.request.Request(
        f"http://127.0.0.1:{port}/index/api/{path}", data=body, method="POST",
        headers={"Content-Type": "application/x-www-form-urlencoded"})
    with urllib.request.urlopen(request, timeout=2) as response:
        assert response.status == 200
        payload = json.loads(response.read().decode("utf-8"))
    assert payload["code"] == 0, payload
    return payload


def write_media_config(source: Path, destination: Path, ports: Dict[str, int]) -> None:
    config = configparser.ConfigParser(interpolation=None, strict=True)
    config.optionxform = str
    with source.open("r", encoding="utf-8-sig") as stream:
        config.read_file(stream)
    config["api"]["secret"] = harness.ZLM_SECRET
    config["http"]["port"] = str(ports["http"])
    config["http"]["sslport"] = "0"
    config["rtsp"]["port"] = str(ports["rtsp"])
    config["rtsp"]["sslport"] = "0"
    config["rtmp"]["port"] = str(ports["rtmp"])
    config["rtmp"]["sslport"] = "0"
    config["rtp_proxy"]["port"] = "0"
    config["shell"]["port"] = "0"
    config["protocol"]["enable_hls"] = "0"
    config["protocol"]["enable_hls_fmp4"] = "0"
    config["protocol"]["enable_mp4"] = "0"
    with destination.open("w", encoding="utf-8") as stream:
        config.write(stream, space_around_delimiters=False)


def media_list(http_port: int, stream_id: str) -> List[Dict[str, Any]]:
    payload = zlm_request(http_port, "getMediaList", {
        "secret": harness.ZLM_SECRET,
        "vhost": "__defaultVhost__",
        "app": "rtp",
        "stream": stream_id,
    })
    return payload.get("data", [])


def rtsp_ready(http_port: int, stream_id: str) -> Optional[Dict[str, Any]]:
    for media in media_list(http_port, stream_id):
        if media.get("schema") == "rtsp" and media.get("stream") == stream_id:
            return media
    return None


def stream_removed(http_port: int, stream_id: str) -> bool:
    return not media_list(http_port, stream_id)


def probe_rtsp(ffprobe: Path, url: str) -> Dict[str, Any]:
    completed = subprocess.run([
        str(ffprobe), "-v", "error", "-rtsp_transport", "tcp",
        "-select_streams", "v:0",
        "-show_entries", "stream=codec_name,codec_type,width,height",
        "-of", "json", url,
    ], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
       text=True, timeout=15)
    payload = json.loads(completed.stdout)
    streams = payload.get("streams", [])
    assert streams, f"ffprobe found no video stream: {completed.stderr}"
    video = streams[0]
    assert video["codec_type"] == "video"
    assert video["codec_name"] == "h264"
    assert video["width"] == 320 and video["height"] == 180
    return video


def decode_rtsp(ffmpeg: Path, url: str) -> None:
    completed = subprocess.run([
        str(ffmpeg), "-v", "error", "-rtsp_transport", "tcp",
        "-i", url, "-map", "0:v:0", "-frames:v", "10", "-f", "null", "-",
    ], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=20)
    assert completed.returncode == 0, completed.stderr


def start_process(arguments: List[str], cwd: Path) -> subprocess.Popen[str]:
    return subprocess.Popen(
        arguments, cwd=str(cwd), stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, text=True)


def run(args: argparse.Namespace) -> None:
    server_path = args.server.resolve()
    media_server_path = args.media_server.resolve()
    simulator_path = args.simulator.resolve()
    ffmpeg_path = Path(args.ffmpeg).resolve()
    ffprobe_path = Path(args.ffprobe).resolve()
    for label, path in (
        ("GbSipServer", server_path),
        ("MediaServer", media_server_path),
        ("device simulator", simulator_path),
        ("ffmpeg", ffmpeg_path),
        ("ffprobe", ffprobe_path),
    ):
        if not path.is_file():
            raise SystemExit(f"{label} does not exist: {path}")

    ports = {
        "sip": harness.free_port(socket.SOCK_STREAM),
        "api": harness.free_port(socket.SOCK_STREAM),
        "http": harness.free_port(socket.SOCK_STREAM),
        "rtsp": harness.free_port(socket.SOCK_STREAM),
        "rtmp": harness.free_port(socket.SOCK_STREAM),
        "device": harness.free_port(socket.SOCK_DGRAM),
    }
    media_server: Optional[subprocess.Popen[str]] = None
    sip_server: Optional[subprocess.Popen[str]] = None
    device: Optional[subprocess.Popen[str]] = None
    succeeded = False
    with tempfile.TemporaryDirectory(prefix="easy-sva-gb28181-real-zlm-") as temp_dir:
        temporary = Path(temp_dir)
        media_config = temporary / "zlm.ini"
        sip_config = temporary / "gb28181.ini"
        log_dir = temporary / "log"
        write_media_config(args.media_config.resolve(), media_config, ports)
        harness.write_config(
            sip_config, ports["sip"], ports["api"], ports["http"], args.rtp_tcp_mode)
        try:
            media_server = start_process([
                str(media_server_path), "-c", str(media_config),
                "--log-dir", str(log_dir), "--threads", "2",
            ], media_server_path.parent)
            harness.wait_for(
                "real ZLMediaKit HTTP API",
                lambda: zlm_request(ports["http"], "getApiList", {
                    "secret": harness.ZLM_SECRET})["data"])

            sip_server = start_process(
                [str(server_path), "--config", str(sip_config)], server_path.parent)
            harness.wait_for(
                "GB28181 control API",
                lambda: harness.api_request(
                    ports["api"], "GET", "/gb28181/api/health")[1]["code"] == 0)

            device = start_process([
                sys.executable, str(simulator_path),
                "--platform-port", str(ports["sip"]),
                "--device-port", str(ports["device"]),
                "--register-expires", "60",
                "--heartbeat-interval", "0.4",
                "--transaction-timeout", "3",
                "--run-seconds", "45",
                "--ffmpeg", str(ffmpeg_path),
                "--width", "320", "--height", "180", "--frame-rate", "10",
                "--media-source-port",
                "0" if args.rtp_tcp_mode == 2 else "30000",
            ], simulator_path.parent)
            harness.wait_for(
                "registered device and heartbeat",
                lambda: harness.assert_registered(ports["api"]))
            harness.wait_for(
                "device catalog", lambda: harness.assert_catalog(ports["api"]))

            status, started = harness.api_request(
                ports["api"], "POST", "/gb28181/api/live/start", {
                    "device_id": harness.DEVICE_ID,
                    "channel_id": harness.CHANNEL_ID,
                })
            assert status == 202 and started["code"] == 0
            session_id = started["data"]["session_id"]
            streaming = harness.wait_for(
                "INVITE/200/ACK streaming state",
                lambda: harness.session_state(ports["api"], session_id, "streaming"))
            stream_id = streaming["stream_id"]
            harness.wait_for(
                "ZLMediaKit RTSP stream",
                lambda: rtsp_ready(ports["http"], stream_id), timeout=15)

            rtsp_url = f"rtsp://127.0.0.1:{ports['rtsp']}/rtp/{stream_id}"
            video = probe_rtsp(ffprobe_path, rtsp_url)
            decode_rtsp(ffmpeg_path, rtsp_url)
            print(
                f"real ZLMediaKit {['UDP', 'TCP passive', 'TCP active'][args.rtp_tcp_mode]} "
                f"RTSP decode passed: {video['codec_name']} "
                f"{video['width']}x{video['height']}, stream={stream_id}")

            status, stopped = harness.api_request(
                ports["api"], "POST", "/gb28181/api/live/stop",
                {"session_id": session_id})
            assert status == 202 and stopped["code"] == 0
            harness.wait_for(
                "BYE/200 stopped state",
                lambda: harness.session_state(ports["api"], session_id, "stopped"))
            harness.wait_for(
                "ZLMediaKit stream removal",
                lambda: stream_removed(ports["http"], stream_id))
            succeeded = True
        finally:
            harness.stop_process(device)
            harness.stop_process(sip_server)
            harness.stop_process(media_server)
            if not succeeded:
                print("--- MediaServer output ---")
                print(harness.process_output(media_server))
                print("--- GbSipServer output ---")
                print(harness.process_output(sip_server))
                print("--- device simulator output ---")
                print(harness.process_output(device))
    print("GB28181 real-ZLMediaKit end-to-end smoke passed")


def main() -> None:
    root = Path(__file__).parents[2]
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", type=Path, required=True)
    parser.add_argument("--media-server", type=Path, required=True)
    parser.add_argument(
        "--media-config", type=Path,
        default=root / "release" / "linux" / "Release" / "config.ini")
    parser.add_argument(
        "--simulator", type=Path,
        default=Path(__file__).parents[1] / "tools" / "gb28181_device_simulator.py")
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument("--ffprobe", default="ffprobe")
    parser.add_argument("--rtp-tcp-mode", type=int, choices=(0, 2), default=0,
                        help="0 tests UDP; 2 tests platform-active TCP/RTP")
    run(parser.parse_args())


if __name__ == "__main__":
    main()
