#!/usr/bin/env python3
"""Send the same SIP OPTIONS request over UDP and TCP and verify both replies."""

import argparse
import socket
import subprocess
import time


def make_request(transport: str, port: int) -> bytes:
    text = (
        "OPTIONS sip:34020000002000000001@3402000000 SIP/2.0\r\n"
        f"Via: SIP/2.0/{transport} 127.0.0.1:25060;branch=z9hG4bK-smoke-{transport.lower()}\r\n"
        "From: <sip:34020000001320000001@3402000000>;tag=smoke-device\r\n"
        "To: <sip:34020000002000000001@3402000000>\r\n"
        f"Call-ID: smoke-{transport.lower()}-{port}\r\n"
        "CSeq: 1 OPTIONS\r\n"
        "Max-Forwards: 70\r\n"
        "Content-Length: 0\r\n\r\n"
    )
    return text.encode("ascii")


def verify_response(response: bytes, transport: str) -> None:
    text = response.decode("ascii")
    assert text.startswith("SIP/2.0 200 OK\r\n"), text
    assert f"Via: SIP/2.0/{transport}" in text, text
    assert "CSeq: 1 OPTIONS\r\n" in text, text
    assert "Allow: REGISTER, MESSAGE, INVITE, ACK, BYE, OPTIONS\r\n" in text, text
    assert "Content-Length: 0\r\n\r\n" in text, text


def smoke_udp(host: str, port: int, timeout: float) -> None:
    request = make_request("UDP", port)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as client:
        client.settimeout(timeout)
        client.sendto(request, (host, port))
        response, _ = client.recvfrom(65535)
    verify_response(response, "UDP")
    print("UDP OPTIONS -> 200 OK")


def smoke_tcp(host: str, port: int, timeout: float) -> None:
    request = make_request("TCP", port)
    with socket.create_connection((host, port), timeout=timeout) as client:
        client.settimeout(timeout)
        split = len(request) // 2
        client.sendall(request[:split])
        client.sendall(request[split:])
        chunks = []
        while True:
            chunk = client.recv(65535)
            if not chunk:
                break
            chunks.append(chunk)
            response = b"".join(chunks)
            if b"\r\n\r\n" in response and response.endswith(b"Content-Length: 0\r\n\r\n"):
                break
    verify_response(b"".join(chunks), "TCP")
    print("TCP OPTIONS -> 200 OK")


def wait_until_listening(host: str, port: int, timeout: float) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            with socket.create_connection((host, port), timeout=0.2):
                return
        except OSError:
            time.sleep(0.05)
    raise TimeoutError(f"GbSipServer did not listen on {host}:{port}")


def run_smoke(host: str, port: int, timeout: float) -> None:
    smoke_udp(host, port, timeout)
    smoke_tcp(host, port, timeout)
    print("GB28181 SIP UDP/TCP transport smoke passed")


def run_managed_server(args: argparse.Namespace) -> None:
    if not args.config:
        raise SystemExit("--config is required when --server is used")
    process = subprocess.Popen([args.server, "--config", args.config])
    try:
        wait_until_listening(args.host, args.port, args.timeout)
        run_smoke(args.host, args.port, args.timeout)
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=2.0)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=2.0)
                raise RuntimeError("GbSipServer did not stop within two seconds after SIGTERM")
    if process.returncode != 0:
        raise RuntimeError(f"GbSipServer exited with status {process.returncode}")
    print("GbSipServer SIGTERM shutdown passed")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=5060)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--server", help="GbSipServer executable to manage during the smoke test")
    parser.add_argument("--config", help="configuration path for a managed GbSipServer")
    args = parser.parse_args()

    if args.server:
        run_managed_server(args)
    else:
        run_smoke(args.host, args.port, args.timeout)


if __name__ == "__main__":
    main()
