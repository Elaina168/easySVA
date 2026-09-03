#!/usr/bin/env python3
"""Exercise SIP transports and the authenticated GB28181 registration flow."""

import argparse
import hashlib
import re
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


def md5_hex(value: str) -> str:
    return hashlib.md5(value.encode("ascii")).hexdigest()


def register_request(port: int, cseq: int, expires: int, authorization: str = "") -> bytes:
    device_id = "34020000001320000001"
    realm = "3402000000"
    uri = f"sip:34020000002000000001@{realm}"
    lines = [
        f"REGISTER {uri} SIP/2.0",
        f"Via: SIP/2.0/UDP 127.0.0.1:25060;branch=z9hG4bK-register-{cseq}",
        f"From: <sip:{device_id}@{realm}>;tag=smoke-register",
        f"To: <sip:{device_id}@{realm}>",
        f"Call-ID: smoke-register-{port}",
        f"CSeq: {cseq} REGISTER",
        f"Contact: <sip:{device_id}@127.0.0.1:25060>",
        "User-Agent: easySVA-smoke-device/1.0",
        f"Expires: {expires}",
    ]
    if authorization:
        lines.append(f"Authorization: {authorization}")
    lines.extend(["Content-Length: 0", "", ""])
    return "\r\n".join(lines).encode("ascii")


def digest_authorization(password: str, nonce: str, cseq: int) -> str:
    username = "34020000001320000001"
    realm = "3402000000"
    uri = f"sip:34020000002000000001@{realm}"
    nc = f"{cseq:08x}"
    cnonce = "easy-sva-smoke"
    ha1 = md5_hex(f"{username}:{realm}:{password}")
    ha2 = md5_hex(f"REGISTER:{uri}")
    response = md5_hex(f"{ha1}:{nonce}:{nc}:{cnonce}:auth:{ha2}")
    return (
        f'Digest username="{username}", realm="{realm}", nonce="{nonce}", '
        f'uri="{uri}", response="{response}", algorithm=MD5, '
        f'qop=auth, nc={nc}, cnonce="{cnonce}"'
    )


def udp_exchange(host: str, port: int, timeout: float, request: bytes) -> str:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as client:
        client.settimeout(timeout)
        client.sendto(request, (host, port))
        response, _ = client.recvfrom(65535)
    return response.decode("ascii")


def message_request(port: int, cseq: int, body: str) -> bytes:
    device_id = "34020000001320000001"
    realm = "3402000000"
    encoded_body = body.encode("utf-8")
    headers = (
        f"MESSAGE sip:34020000002000000001@{realm} SIP/2.0\r\n"
        f"Via: SIP/2.0/UDP 127.0.0.1:25060;branch=z9hG4bK-message-{cseq}\r\n"
        f"From: <sip:{device_id}@{realm}>;tag=smoke-register\r\n"
        f"To: <sip:34020000002000000001@{realm}>\r\n"
        f"Call-ID: smoke-message-{port}\r\n"
        f"CSeq: {cseq} MESSAGE\r\n"
        "Content-Type: Application/MANSCDP+xml; charset=UTF-8\r\n"
        f"Content-Length: {len(encoded_body)}\r\n\r\n"
    ).encode("ascii")
    return headers + encoded_body


def smoke_device_messages(host: str, port: int, timeout: float) -> None:
    device_id = "34020000001320000001"
    keepalive = (
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<Notify><CmdType>Keepalive</CmdType><SN>1</SN>"
        f"<DeviceID>{device_id}</DeviceID><Status>OK</Status></Notify>"
    )
    response = udp_exchange(host, port, timeout, message_request(port, 1, keepalive))
    assert response.startswith("SIP/2.0 200 OK\r\n"), response

    catalog = (
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<Response><CmdType>Catalog</CmdType><SN>2</SN>"
        f"<DeviceID>{device_id}</DeviceID><SumNum>1</SumNum>"
        "<DeviceList Num=\"1\"><Item>"
        "<DeviceID>34020000001320000002</DeviceID><Name>入口摄像机</Name>"
        "<Manufacturer>easySVA</Manufacturer><Model>Smoke-IPC</Model>"
        f"<ParentID>{device_id}</ParentID><Parental>0</Parental>"
        "<Status>ON</Status><Longitude>116.3</Longitude><Latitude>39.9</Latitude>"
        "</Item></DeviceList></Response>"
    )
    response = udp_exchange(host, port, timeout, message_request(port, 2, catalog))
    assert response.startswith("SIP/2.0 200 OK\r\n"), response
    print("UDP MESSAGE Keepalive/Catalog -> 200/200")


def smoke_registration(host: str, port: int, timeout: float, password: str) -> None:
    challenge = udp_exchange(host, port, timeout, register_request(port, 1, 3600))
    assert challenge.startswith("SIP/2.0 401 Unauthorized\r\n"), challenge
    match = re.search(r'nonce="([^"]+)"', challenge)
    assert match, challenge
    nonce = match.group(1)

    registered = udp_exchange(
        host, port, timeout,
        register_request(port, 2, 3600, digest_authorization(password, nonce, 2)),
    )
    assert registered.startswith("SIP/2.0 200 OK\r\n"), registered
    assert "Expires: 3600\r\n" in registered, registered

    smoke_device_messages(host, port, timeout)

    renewed = udp_exchange(
        host, port, timeout,
        register_request(port, 3, 300, digest_authorization(password, nonce, 3)),
    )
    assert renewed.startswith("SIP/2.0 200 OK\r\n"), renewed
    assert "Expires: 300\r\n" in renewed, renewed

    logged_out = udp_exchange(
        host, port, timeout,
        register_request(port, 4, 0, digest_authorization(password, nonce, 4)),
    )
    assert logged_out.startswith("SIP/2.0 200 OK\r\n"), logged_out
    assert "Expires: 0\r\n" in logged_out, logged_out
    print("UDP REGISTER challenge/auth/renew/logout -> 401/200/200/200")


def wait_until_listening(host: str, port: int, timeout: float) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            with socket.create_connection((host, port), timeout=0.2):
                return
        except OSError:
            time.sleep(0.05)
    raise TimeoutError(f"GbSipServer did not listen on {host}:{port}")


def run_smoke(host: str, port: int, timeout: float, password: str) -> None:
    smoke_udp(host, port, timeout)
    smoke_tcp(host, port, timeout)
    smoke_registration(host, port, timeout, password)
    print("GB28181 SIP UDP/TCP transport smoke passed")


def run_managed_server(args: argparse.Namespace) -> None:
    if not args.config:
        raise SystemExit("--config is required when --server is used")
    process = subprocess.Popen([args.server, "--config", args.config])
    try:
        wait_until_listening(args.host, args.port, args.timeout)
        run_smoke(args.host, args.port, args.timeout, args.password)
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
    parser.add_argument("--password", default="12345678", help="demo device Digest password")
    args = parser.parse_args()

    if args.server:
        run_managed_server(args)
    else:
        run_smoke(args.host, args.port, args.timeout, args.password)


if __name__ == "__main__":
    main()
