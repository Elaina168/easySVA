#!/usr/bin/env python3
"""End-to-end GB28181 device, control API and PS/RTP smoke test."""

from __future__ import annotations

import argparse
import json
import os
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import urllib.parse
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional, Tuple


DEVICE_ID = "34020000001320000001"
CHANNEL_ID = "34020000001320000002"
PLATFORM_ID = "34020000002000000001"
API_SECRET = "e2e-api-secret"
ZLM_SECRET = "e2e-zlm-secret"


def free_port(sock_type: int) -> int:
    with socket.socket(socket.AF_INET, sock_type) as probe:
        probe.bind(("127.0.0.1", 0))
        return int(probe.getsockname()[1])


def wait_for(description: str, predicate: Callable[[], Any], timeout: float = 8.0) -> Any:
    deadline = time.monotonic() + timeout
    last_error: Optional[Exception] = None
    while time.monotonic() < deadline:
        try:
            result = predicate()
            if result:
                return result
        except (OSError, AssertionError, KeyError, IndexError) as ex:
            last_error = ex
        time.sleep(0.05)
    suffix = f": {last_error}" if last_error else ""
    raise TimeoutError(f"timed out waiting for {description}{suffix}")


class FakeZlm:
    def __init__(self, port: int) -> None:
        self.port = port
        self.rtp_socket: Optional[socket.socket] = None
        self.rtp_port = 0
        self.expected_ssrc = 0
        self.stream_id = ""
        self.packets: List[bytes] = []
        self.close_calls = 0
        self._running = True
        self._receiver: Optional[threading.Thread] = None
        owner = self

        class Handler(BaseHTTPRequestHandler):
            def do_POST(self) -> None:
                length = int(self.headers.get("Content-Length", "0"))
                body = self.rfile.read(length).decode("utf-8")
                form = urllib.parse.parse_qs(body)
                if form.get("secret", [""])[0] != ZLM_SECRET:
                    self.reply({"code": -100, "msg": "bad secret"})
                    return
                if self.path == "/index/api/openRtpServer":
                    owner.open_rtp(form)
                    self.reply({"code": 0, "port": owner.rtp_port})
                    return
                if self.path == "/index/api/closeRtpServer":
                    owner.close_calls += 1
                    self.reply({"code": 0, "hit": 1})
                    return
                self.send_error(404)

            def reply(self, value: Dict[str, Any]) -> None:
                data = json.dumps(value).encode("utf-8")
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)

            def log_message(self, _format: str, *_args: object) -> None:
                return

        self.server = ThreadingHTTPServer(("127.0.0.1", port), Handler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)

    def start(self) -> None:
        self.thread.start()

    def open_rtp(self, form: Dict[str, List[str]]) -> None:
        if self.rtp_socket:
            raise RuntimeError("test received a duplicate openRtpServer call")
        self.expected_ssrc = int(form["ssrc"][0])
        self.stream_id = form["stream_id"][0]
        self.rtp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.rtp_socket.bind(("127.0.0.1", 0))
        self.rtp_socket.settimeout(0.2)
        self.rtp_port = int(self.rtp_socket.getsockname()[1])
        self._receiver = threading.Thread(target=self.receive_rtp, daemon=True)
        self._receiver.start()

    def receive_rtp(self) -> None:
        assert self.rtp_socket is not None
        while self._running:
            try:
                packet, _ = self.rtp_socket.recvfrom(65535)
            except socket.timeout:
                continue
            except OSError:
                return
            self.packets.append(packet)

    def stop(self) -> None:
        self._running = False
        if self.rtp_socket:
            self.rtp_socket.close()
        if self._receiver:
            self._receiver.join(timeout=1)
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=1)


def api_request(port: int, method: str, path: str, body: Optional[Dict[str, str]] = None) -> Tuple[int, Dict[str, Any]]:
    data = json.dumps(body).encode("utf-8") if body is not None else None
    request = urllib.request.Request(
        f"http://127.0.0.1:{port}{path}", data=data, method=method,
        headers={
            "Authorization": f"Bearer {API_SECRET}",
            "Content-Type": "application/json",
        })
    with urllib.request.urlopen(request, timeout=2) as response:
        return response.status, json.loads(response.read().decode("utf-8"))


def write_config(path: Path, sip_port: int, api_port: int, zlm_port: int) -> None:
    path.write_text(
        f"""[sip]
server_id={PLATFORM_ID}
realm=3402000000
advertised_ip=127.0.0.1
listen_ip=127.0.0.1
port={sip_port}
udp=true
tcp=true
idle_timeout_seconds=30
transaction_timeout_seconds=3
max_message_bytes=1048576

[registration]
auth_required=true
device_password=12345678
nonce_ttl_seconds=300
default_expires_seconds=60
min_expires_seconds=10
max_expires_seconds=3600

[device]
heartbeat_timeout_seconds=3

[api]
enabled=true
listen_ip=127.0.0.1
port={api_port}
secret={API_SECRET}

[media]
zlm_api_url=http://127.0.0.1:{zlm_port}
zlm_api_secret={ZLM_SECRET}
zlm_api_timeout_seconds=3
rtp_advertised_ip=127.0.0.1
rtp_listen_ip=127.0.0.1
rtp_port=0
rtp_tcp_mode=0
""",
        encoding="utf-8")


def process_output(process: Optional[subprocess.Popen[str]]) -> str:
    if not process or not process.stdout:
        return ""
    return process.stdout.read()


def stop_process(process: Optional[subprocess.Popen[str]]) -> None:
    if not process or process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=2)


def assert_registered(api_port: int) -> Dict[str, Any]:
    status, payload = api_request(api_port, "GET", "/gb28181/api/devices")
    assert status == 200 and payload["code"] == 0
    devices = payload["data"]
    assert len(devices) == 1 and devices[0]["device_id"] == DEVICE_ID
    assert devices[0]["online"] and devices[0]["last_heartbeat_at"] > 0
    return devices[0]


def assert_catalog(api_port: int) -> Dict[str, Any]:
    status, payload = api_request(
        api_port, "GET", f"/gb28181/api/catalog?device_id={DEVICE_ID}")
    assert status == 200 and payload["code"] == 0
    channels = payload["data"]
    assert len(channels) == 1 and channels[0]["channel_id"] == CHANNEL_ID
    assert channels[0]["status"] == "ON"
    return channels[0]


def command_succeeded(api_port: int, command_id: str) -> bool:
    _, payload = api_request(
        api_port, "GET", f"/gb28181/api/commands?command_id={command_id}")
    return payload["data"]["state"] == "succeeded"


def session_state(api_port: int, session_id: str, expected: str) -> Optional[Dict[str, Any]]:
    _, payload = api_request(api_port, "GET", "/gb28181/api/sessions")
    for session in payload["data"]:
        if session["session_id"] == session_id and session["state"] == expected:
            return session
    return None


def valid_rtp_packet(fake_zlm: FakeZlm) -> Optional[bytes]:
    for packet in fake_zlm.packets:
        if len(packet) < 16:
            continue
        version, payload_type, _sequence, _timestamp, ssrc = struct.unpack("!BBHII", packet[:12])
        if version == 0x80 and (payload_type & 0x7F) == 96 and ssrc == fake_zlm.expected_ssrc:
            if packet[12:16] == b"\x00\x00\x01\xba":
                return packet
    return None


def run(args: argparse.Namespace) -> None:
    server_path = args.server.resolve()
    simulator_path = args.simulator.resolve()
    if not server_path.is_file():
        raise SystemExit(f"GbSipServer does not exist: {server_path}")
    if not simulator_path.is_file():
        raise SystemExit(f"device simulator does not exist: {simulator_path}")

    sip_port = free_port(socket.SOCK_STREAM)
    api_port = free_port(socket.SOCK_STREAM)
    zlm_port = free_port(socket.SOCK_STREAM)
    device_port = free_port(socket.SOCK_DGRAM)
    fake_zlm = FakeZlm(zlm_port)
    server: Optional[subprocess.Popen[str]] = None
    device: Optional[subprocess.Popen[str]] = None
    succeeded = False
    with tempfile.TemporaryDirectory(prefix="easy-sva-gb28181-e2e-") as temp_dir:
        config_path = Path(temp_dir) / "gb28181.ini"
        write_config(config_path, sip_port, api_port, zlm_port)
        try:
            fake_zlm.start()
            server = subprocess.Popen(
                [str(server_path), "--config", str(config_path)],
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                text=True, env=os.environ.copy())
            wait_for(
                "GB28181 control API",
                lambda: api_request(api_port, "GET", "/gb28181/api/health")[1]["code"] == 0)

            device = subprocess.Popen([
                sys.executable, str(simulator_path),
                "--platform-port", str(sip_port),
                "--device-port", str(device_port),
                "--register-expires", "60",
                "--heartbeat-interval", "0.4",
                "--transaction-timeout", "3",
                "--run-seconds", "30",
                "--ffmpeg", args.ffmpeg,
                "--width", "320", "--height", "180", "--frame-rate", "10",
            ], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

            wait_for("registered device and heartbeat", lambda: assert_registered(api_port))
            wait_for("initial device catalog", lambda: assert_catalog(api_port))
            print("REGISTER + Digest + Keepalive + initial Catalog passed")

            status, command = api_request(
                api_port, "POST", "/gb28181/api/catalog/query", {"device_id": DEVICE_ID})
            assert status == 202 and command["code"] == 0
            command_id = command["data"]["command_id"]
            wait_for("Catalog query completion", lambda: command_succeeded(api_port, command_id))
            print("platform Catalog query/response transaction passed")

            status, started = api_request(
                api_port, "POST", "/gb28181/api/live/start",
                {"device_id": DEVICE_ID, "channel_id": CHANNEL_ID})
            assert status == 202 and started["code"] == 0
            session_id = started["data"]["session_id"]
            streaming = wait_for(
                "INVITE/200/ACK streaming state",
                lambda: session_state(api_port, session_id, "streaming"))
            assert streaming["ssrc"] == f"{fake_zlm.expected_ssrc:010d}"
            wait_for("PS-over-RTP packet", lambda: valid_rtp_packet(fake_zlm), timeout=12)
            print(
                f"INVITE/200/ACK + PS/RTP passed: stream={fake_zlm.stream_id}, "
                f"port={fake_zlm.rtp_port}, ssrc={fake_zlm.expected_ssrc:010d}")

            status, stopped = api_request(
                api_port, "POST", "/gb28181/api/live/stop", {"session_id": session_id})
            assert status == 202 and stopped["code"] == 0
            wait_for("BYE/200 stopped state", lambda: session_state(api_port, session_id, "stopped"))
            wait_for("closeRtpServer call", lambda: fake_zlm.close_calls == 1)
            print("BYE/200 + RTP receiver release passed")
            succeeded = True
        finally:
            stop_process(device)
            stop_process(server)
            fake_zlm.stop()
            if not succeeded:
                print("--- GbSipServer output ---")
                print(process_output(server))
                print("--- device simulator output ---")
                print(process_output(device))
    print("GB28181 reproducible device end-to-end smoke passed")


def main() -> None:
    default_simulator = Path(__file__).parents[1] / "tools" / "gb28181_device_simulator.py"
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", type=Path, required=True)
    parser.add_argument("--simulator", type=Path, default=default_simulator)
    parser.add_argument("--ffmpeg", default="ffmpeg")
    run(parser.parse_args())


if __name__ == "__main__":
    main()
