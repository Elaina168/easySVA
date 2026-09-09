#!/usr/bin/env python3
"""A reproducible GB28181 UDP device with Digest, heartbeat, catalog and PS/RTP."""

from __future__ import annotations

import argparse
import hashlib
import html
import os
import random
import re
import signal
import socket
import struct
import subprocess
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, Iterable, Iterator, List, Optional, Sequence, Tuple


CRLF = "\r\n"
PS_PACK_START = b"\x00\x00\x01\xba"


@dataclass
class SipMessage:
    start_line: str
    headers: List[Tuple[str, str]] = field(default_factory=list)
    body: bytes = b""

    @classmethod
    def parse(cls, wire: bytes) -> "SipMessage":
        header_bytes, delimiter, remainder = wire.partition(b"\r\n\r\n")
        if not delimiter:
            header_bytes, delimiter, remainder = wire.partition(b"\n\n")
        if not delimiter:
            raise ValueError("incomplete SIP headers")
        lines = header_bytes.decode("utf-8").replace("\r\n", "\n").split("\n")
        if not lines or not lines[0]:
            raise ValueError("empty SIP start line")
        headers: List[Tuple[str, str]] = []
        for line in lines[1:]:
            if line[:1] in (" ", "\t") and headers:
                name, value = headers[-1]
                headers[-1] = (name, value + " " + line.strip())
                continue
            if ":" not in line:
                raise ValueError("invalid SIP header")
            name, value = line.split(":", 1)
            headers.append((name.strip(), value.strip()))
        message = cls(lines[0], headers)
        length_text = message.header("Content-Length", "0")
        try:
            length = int(length_text)
        except ValueError as ex:
            raise ValueError("invalid SIP Content-Length") from ex
        if length < 0 or len(remainder) < length:
            raise ValueError("incomplete SIP body")
        message.body = remainder[:length]
        return message

    @property
    def method(self) -> str:
        return self.start_line.split(" ", 1)[0] if not self.is_response else ""

    @property
    def status_code(self) -> int:
        if not self.is_response:
            return 0
        parts = self.start_line.split(" ", 2)
        return int(parts[1]) if len(parts) > 1 else 0

    @property
    def is_response(self) -> bool:
        return self.start_line.startswith("SIP/")

    def header(self, name: str, default: str = "") -> str:
        lowered = name.lower()
        for header_name, value in self.headers:
            if header_name.lower() == lowered:
                return value
        return default

    def header_values(self, name: str) -> List[str]:
        lowered = name.lower()
        return [value for header_name, value in self.headers
                if header_name.lower() == lowered]

    def serialize(self) -> bytes:
        headers = [(name, value) for name, value in self.headers
                   if name.lower() != "content-length"]
        headers.append(("Content-Length", str(len(self.body))))
        lines = [self.start_line]
        lines.extend(f"{name}: {value}" for name, value in headers)
        return (CRLF.join(lines) + CRLF + CRLF).encode("utf-8") + self.body


@dataclass(frozen=True)
class MediaOffer:
    host: str
    port: int
    ssrc: int
    payload_type: int
    tcp: bool
    setup: str


@dataclass
class MediaDialog:
    invite: SipMessage
    response: SipMessage
    offer: MediaOffer
    pusher: Optional["PsRtpPusher"] = None


def parse_auth_parameters(value: str) -> Dict[str, str]:
    scheme, separator, parameters = value.partition(" ")
    if not separator or scheme.lower() != "digest":
        raise ValueError("unsupported authentication scheme")
    parsed: Dict[str, str] = {}
    pattern = re.compile(r'(\w+)\s*=\s*("(?:[^"\\]|\\.)*"|[^,\s]+)')
    for match in pattern.finditer(parameters):
        item = match.group(2)
        if item.startswith('"') and item.endswith('"'):
            item = item[1:-1].replace('\\"', '"')
        parsed[match.group(1).lower()] = item
    return parsed


def md5_hex(value: str) -> str:
    return hashlib.md5(value.encode("utf-8")).hexdigest()


def digest_authorization(
    challenge: str,
    username: str,
    password: str,
    method: str,
    uri: str,
    nonce_count: int,
    client_nonce: str,
) -> str:
    parameters = parse_auth_parameters(challenge)
    realm = parameters.get("realm", "")
    nonce = parameters.get("nonce", "")
    algorithm = parameters.get("algorithm", "MD5")
    if not realm or not nonce or algorithm.lower() != "md5":
        raise ValueError("Digest challenge must contain realm, nonce and MD5")
    ha1 = md5_hex(f"{username}:{realm}:{password}")
    ha2 = md5_hex(f"{method}:{uri}")
    qop_values = [item.strip().lower()
                  for item in parameters.get("qop", "").split(",") if item.strip()]
    if qop_values:
        if "auth" not in qop_values:
            raise ValueError("Digest qop=auth is required")
        nc = f"{nonce_count:08x}"
        response = md5_hex(f"{ha1}:{nonce}:{nc}:{client_nonce}:auth:{ha2}")
        return (
            f'Digest username="{username}", realm="{realm}", nonce="{nonce}", '
            f'uri="{uri}", response="{response}", algorithm=MD5, '
            f'qop=auth, nc={nc}, cnonce="{client_nonce}"'
        )
    response = md5_hex(f"{ha1}:{nonce}:{ha2}")
    return (
        f'Digest username="{username}", realm="{realm}", nonce="{nonce}", '
        f'uri="{uri}", response="{response}", algorithm=MD5'
    )


def parse_sdp_offer(body: bytes) -> MediaOffer:
    text = body.decode("utf-8")
    connection = re.search(r"(?im)^c=IN IP4\s+(\S+)\s*$", text)
    media = re.search(r"(?im)^m=video\s+(\d+)\s+((?:TCP/)?RTP/AVP)\s+(\d+)\s*$", text)
    ssrc = re.search(r"(?im)^y=(\d{10})\s*$", text)
    codec = re.search(r"(?im)^a=rtpmap:(\d+)\s+PS/90000\s*$", text)
    setup = re.search(r"(?im)^a=setup:(active|passive)\s*$", text)
    if not connection or not media or not ssrc or not codec:
        raise ValueError("INVITE SDP must contain IPv4, video, PS/90000 and y=SSRC")
    payload_type = int(media.group(3))
    if payload_type != int(codec.group(1)):
        raise ValueError("SDP PS payload type does not match the media line")
    port = int(media.group(1))
    if port < 1 or port > 65535:
        raise ValueError("SDP media port is outside 1..65535")
    return MediaOffer(
        host=connection.group(1),
        port=port,
        ssrc=int(ssrc.group(1)),
        payload_type=payload_type,
        tcp=media.group(2).upper() != "RTP/AVP",
        setup=setup.group(1).lower() if setup else "",
    )


def iter_ps_packs(chunks: Iterable[bytes]) -> Iterator[bytes]:
    buffer = bytearray()
    for chunk in chunks:
        if not chunk:
            continue
        buffer.extend(chunk)
        while True:
            first = buffer.find(PS_PACK_START)
            if first < 0:
                if len(buffer) > 3:
                    del buffer[:-3]
                break
            if first:
                del buffer[:first]
            second = buffer.find(PS_PACK_START, len(PS_PACK_START))
            if second < 0:
                break
            yield bytes(buffer[:second])
            del buffer[:second]
    if buffer.startswith(PS_PACK_START):
        yield bytes(buffer)


def packetize_rtp(
    payload: bytes,
    payload_type: int,
    sequence: int,
    timestamp: int,
    ssrc: int,
    maximum_payload: int = 1200,
) -> Iterator[Tuple[bytes, int]]:
    for offset in range(0, len(payload), maximum_payload):
        part = payload[offset:offset + maximum_payload]
        marker = 0x80 if offset + maximum_payload >= len(payload) else 0
        header = struct.pack(
            "!BBHII",
            0x80,
            marker | payload_type,
            sequence & 0xFFFF,
            timestamp & 0xFFFFFFFF,
            ssrc & 0xFFFFFFFF,
        )
        yield header + part, (sequence + 1) & 0xFFFF
        sequence = (sequence + 1) & 0xFFFF


def frame_tcp_rtp(packet: bytes) -> bytes:
    if len(packet) > 65535:
        raise ValueError("RFC 4571 RTP packet exceeds 65535 bytes")
    return struct.pack("!H", len(packet)) + packet


class PsRtpPusher:
    def __init__(
        self,
        offer: MediaOffer,
        ffmpeg: str,
        source: Optional[Path],
        width: int,
        height: int,
        frame_rate: int,
        listen_ip: str,
        listen_port: int,
    ) -> None:
        self.offer = offer
        self.ffmpeg = ffmpeg
        self.source = source
        self.width = width
        self.height = height
        self.frame_rate = frame_rate
        self.listen_ip = listen_ip
        self.requested_listen_port = listen_port
        self._stop = threading.Event()
        self._thread: Optional[threading.Thread] = None
        self._process: Optional[subprocess.Popen[bytes]] = None
        self._listener: Optional[socket.socket] = None
        self._connection: Optional[socket.socket] = None
        self.packet_count = 0

    @property
    def listen_port(self) -> int:
        return int(self._listener.getsockname()[1]) if self._listener else 0

    @property
    def started(self) -> bool:
        return bool(self._thread and self._thread.is_alive())

    def prepare(self) -> int:
        if not self.offer.tcp:
            return self.offer.port
        if self.offer.setup != "active":
            raise ValueError("TCP simulator requires an SDP offer with setup:active")
        if not self._listener:
            listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            listener.bind((self.listen_ip, self.requested_listen_port))
            listener.listen(1)
            listener.settimeout(0.2)
            self._listener = listener
        return self.listen_port

    def start(self) -> None:
        if self.offer.tcp:
            self.prepare()
        self._thread = threading.Thread(target=self._run, name="gb28181-ps-rtp", daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        if self._connection:
            self._connection.close()
        if self._listener:
            self._listener.close()
        process = self._process
        if process and process.poll() is None:
            process.terminate()
        if self._thread and self._thread is not threading.current_thread():
            self._thread.join(timeout=3)
        if process and process.poll() is None:
            process.kill()
            process.wait(timeout=2)

    def _command(self) -> List[str]:
        command = [self.ffmpeg, "-hide_banner", "-loglevel", "error", "-re", "-fflags", "+genpts"]
        if self.source:
            command.extend(["-stream_loop", "-1", "-i", str(self.source)])
        else:
            command.extend([
                "-f", "lavfi", "-i",
                f"testsrc2=size={self.width}x{self.height}:rate={self.frame_rate}",
            ])
        command.extend([
            "-map", "0:v:0", "-an", "-c:v", "libx264", "-preset", "ultrafast",
            "-tune", "zerolatency", "-pix_fmt", "yuv420p", "-g", str(self.frame_rate),
            "-bf", "0", "-f", "mpeg", "-muxdelay", "0", "-muxpreload", "0",
            "-flush_packets", "1", "pipe:1",
        ])
        return command

    def _chunks(self, stream: object) -> Iterator[bytes]:
        while not self._stop.is_set():
            chunk = stream.read1(8192)  # type: ignore[attr-defined]
            if not chunk:
                return
            yield chunk

    def _transport(self) -> socket.socket:
        if not self.offer.tcp:
            return socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.prepare()
        assert self._listener is not None
        while not self._stop.is_set():
            try:
                connection, _ = self._listener.accept()
                self._connection = connection
                return connection
            except socket.timeout:
                continue
        raise OSError("TCP media listener stopped before ZLMediaKit connected")

    def _send_packet(self, transport: socket.socket, packet: bytes) -> None:
        if self.offer.tcp:
            transport.sendall(frame_tcp_rtp(packet))
        else:
            transport.sendto(packet, (self.offer.host, self.offer.port))

    def _run(self) -> None:
        sequence = random.randrange(0, 65536)
        started = time.monotonic()
        transport: Optional[socket.socket] = None
        try:
            transport = self._transport()
            self._process = subprocess.Popen(
                self._command(), stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
            assert self._process.stdout is not None
            for pack in iter_ps_packs(self._chunks(self._process.stdout)):
                timestamp = int((time.monotonic() - started) * 90000)
                for packet, sequence in packetize_rtp(
                    pack, self.offer.payload_type, sequence, timestamp, self.offer.ssrc
                ):
                    if self._stop.is_set():
                        return
                    self._send_packet(transport, packet)
                    self.packet_count += 1
        finally:
            if transport:
                transport.close()
            process = self._process
            if process and process.poll() is None:
                process.terminate()
            if process:
                process.wait(timeout=2)
                if process.returncode not in (0, -signal.SIGTERM) and not self._stop.is_set():
                    error = "(stderr suppressed)"
                    print(f"media generator stopped: {error.strip()}", flush=True)


class GbDeviceSimulator:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.bind((args.bind_ip, args.device_port))
        self.socket.settimeout(0.5)
        self.platform = (args.platform_host, args.platform_port)
        self.running = True
        self.register_call_id = f"register-{random.randrange(1 << 48):012x}"
        self.device_tag = f"device-{random.randrange(1 << 32):08x}"
        self.client_nonce = f"cnonce-{random.randrange(1 << 32):08x}"
        self.cseq = 0
        self.message_serial = 0
        self.nonce_count = 0
        self.challenge = ""
        self.dialogs: Dict[str, MediaDialog] = {}

    @property
    def platform_uri(self) -> str:
        return f"sip:{self.args.platform_id}@{self.args.realm}"

    @property
    def device_uri(self) -> str:
        return f"sip:{self.args.device_id}@{self.args.realm}"

    def next_cseq(self) -> int:
        self.cseq += 1
        return self.cseq

    def next_serial(self) -> int:
        self.message_serial += 1
        return self.message_serial

    def request(
        self,
        method: str,
        uri: str,
        body: bytes = b"",
        content_type: str = "",
        call_id: str = "",
        cseq: Optional[int] = None,
        authorization: str = "",
        expires: Optional[int] = None,
    ) -> SipMessage:
        sequence = cseq if cseq is not None else self.next_cseq()
        branch = f"z9hG4bK-device-{sequence}-{random.randrange(1 << 24):06x}"
        headers = [
            ("Via", f"SIP/2.0/UDP {self.args.advertised_ip}:{self.args.device_port};branch={branch}"),
            ("From", f"<{self.device_uri}>;tag={self.device_tag}"),
            ("To", f"<{self.device_uri}>" if method == "REGISTER" else f"<{self.platform_uri}>"),
            ("Call-ID", call_id or f"message-{sequence}-{self.register_call_id}"),
            ("CSeq", f"{sequence} {method}"),
            ("Max-Forwards", "70"),
            ("Contact", f"<sip:{self.args.device_id}@{self.args.advertised_ip}:{self.args.device_port}>"),
            ("User-Agent", "easySVA-GB28181-simulator/1.0"),
        ]
        if expires is not None:
            headers.append(("Expires", str(expires)))
        if authorization:
            headers.append(("Authorization", authorization))
        if content_type:
            headers.append(("Content-Type", content_type))
        return SipMessage(f"{method} {uri} SIP/2.0", headers, body)

    def send(self, message: SipMessage) -> None:
        self.socket.sendto(message.serialize(), self.platform)

    def receive_response(self, call_id: str, cseq: int, timeout: float) -> SipMessage:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self.socket.settimeout(max(0.05, deadline - time.monotonic()))
            wire, _ = self.socket.recvfrom(1024 * 1024)
            message = SipMessage.parse(wire)
            if message.is_response and message.header("Call-ID") == call_id:
                if message.header("CSeq").split(" ", 1)[0] == str(cseq):
                    return message
            self.handle_message(message)
        raise TimeoutError("SIP transaction timed out")

    def register(self, expires: int) -> None:
        sequence = self.next_cseq()
        authorization = ""
        if self.challenge:
            self.nonce_count += 1
            authorization = digest_authorization(
                self.challenge, self.args.device_id, self.args.password,
                "REGISTER", self.platform_uri, self.nonce_count, self.client_nonce)
        request = self.request(
            "REGISTER", self.platform_uri, call_id=self.register_call_id,
            cseq=sequence, authorization=authorization, expires=expires)
        self.send(request)
        response = self.receive_response(
            self.register_call_id, sequence, self.args.transaction_timeout)
        if response.status_code == 401 and not authorization:
            self.challenge = response.header("WWW-Authenticate")
            self.register(expires)
            return
        if response.status_code != 200:
            raise RuntimeError(f"REGISTER failed with SIP {response.status_code}")
        action = "unregistered" if expires == 0 else "registered"
        print(f"SIP {action}: device={self.args.device_id}, expires={expires}", flush=True)

    def xml_message(self, body: str) -> None:
        message = self.request(
            "MESSAGE", self.platform_uri, body.encode("utf-8"),
            "Application/MANSCDP+xml; charset=UTF-8")
        self.send(message)

    def send_keepalive(self) -> None:
        serial = self.next_serial()
        body = (
            '<?xml version="1.0" encoding="UTF-8"?>\r\n'
            "<Notify>\r\n"
            "<CmdType>Keepalive</CmdType>\r\n"
            f"<SN>{serial}</SN>\r\n"
            f"<DeviceID>{self.args.device_id}</DeviceID>\r\n"
            "<Status>OK</Status>\r\n"
            "</Notify>\r\n"
        )
        self.xml_message(body)
        print(f"heartbeat sent: SN={serial}", flush=True)

    def send_catalog(self, serial: Optional[str] = None) -> None:
        response_serial = serial or str(self.next_serial())
        device_id = html.escape(self.args.device_id)
        channel_id = html.escape(self.args.channel_id)
        channel_name = html.escape(self.args.channel_name)
        body = (
            '<?xml version="1.0" encoding="UTF-8"?>\r\n'
            "<Response>\r\n"
            "<CmdType>Catalog</CmdType>\r\n"
            f"<SN>{response_serial}</SN>\r\n"
            f"<DeviceID>{device_id}</DeviceID>\r\n"
            "<SumNum>1</SumNum>\r\n"
            '<DeviceList Num="1">\r\n'
            "<Item>\r\n"
            f"<DeviceID>{channel_id}</DeviceID>\r\n"
            f"<Name>{channel_name}</Name>\r\n"
            "<Manufacturer>easySVA</Manufacturer>\r\n"
            "<Model>Reproducible-Simulator</Model>\r\n"
            f"<ParentID>{device_id}</ParentID>\r\n"
            "<Parental>0</Parental>\r\n"
            "<Status>ON</Status>\r\n"
            "</Item>\r\n"
            "</DeviceList>\r\n"
            "</Response>\r\n"
        )
        self.xml_message(body)
        print(f"catalog sent: channel={self.args.channel_id}, SN={response_serial}", flush=True)

    def response(
        self,
        request: SipMessage,
        status: int,
        reason: str,
        body: bytes = b"",
        content_type: str = "",
    ) -> SipMessage:
        to_header = request.header("To")
        if ";tag=" not in to_header.lower():
            to_header += f";tag={self.device_tag}"
        headers: List[Tuple[str, str]] = []
        headers.extend(("Via", value) for value in request.header_values("Via"))
        headers.extend([
            ("From", request.header("From")),
            ("To", to_header),
            ("Call-ID", request.header("Call-ID")),
            ("CSeq", request.header("CSeq")),
        ])
        if status == 200 and request.method == "INVITE":
            headers.append(("Contact", f"<sip:{self.args.channel_id}@{self.args.advertised_ip}:{self.args.device_port}>"))
        if content_type:
            headers.append(("Content-Type", content_type))
        return SipMessage(f"SIP/2.0 {status} {reason}", headers, body)

    def send_response(self, response: SipMessage) -> None:
        self.send(response)

    def handle_catalog_query(self, request: SipMessage) -> None:
        self.send_response(self.response(request, 200, "OK"))
        body = request.body.decode("utf-8", errors="replace")
        match = re.search(r"<SN>\s*([^<]+)\s*</SN>", body, re.IGNORECASE)
        self.send_catalog(match.group(1).strip() if match else None)

    def handle_invite(self, request: SipMessage) -> None:
        call_id = request.header("Call-ID")
        pusher: Optional[PsRtpPusher] = None
        try:
            offer = parse_sdp_offer(request.body)
            if offer.tcp and offer.setup != "active":
                raise ValueError("the simulator supports TCP media for platform-active mode")
            if offer.tcp:
                pusher = PsRtpPusher(
                    offer, self.args.ffmpeg, self.args.input,
                    self.args.width, self.args.height, self.args.frame_rate,
                    self.args.bind_ip, self.args.media_source_port)
                answer_port = pusher.prepare()
            else:
                answer_port = self.args.media_source_port
        except ValueError as ex:
            self.send_response(self.response(request, 488, "Not Acceptable Here"))
            print(f"INVITE rejected: {ex}", flush=True)
            return
        transport = "TCP/RTP/AVP" if offer.tcp else "RTP/AVP"
        tcp_attributes = (
            "a=setup:passive\r\na=connection:new\r\n" if offer.tcp else "")
        sdp = (
            "v=0\r\n"
            f"o={self.args.device_id} 0 0 IN IP4 {self.args.advertised_ip}\r\n"
            "s=Play\r\n"
            f"c=IN IP4 {self.args.advertised_ip}\r\n"
            "t=0 0\r\n"
            f"m=video {answer_port} {transport} {offer.payload_type}\r\n"
            "a=sendonly\r\n"
            f"{tcp_attributes}"
            f"a=rtpmap:{offer.payload_type} PS/90000\r\n"
            f"y={offer.ssrc:010d}\r\n"
        ).encode("ascii")
        accepted = self.response(request, 200, "OK", sdp, "application/sdp")
        self.dialogs[call_id] = MediaDialog(request, accepted, offer, pusher)
        self.send_response(accepted)
        print(
            f"INVITE accepted: call={call_id}, transport={'TCP' if offer.tcp else 'UDP'}, "
            f"RTP={offer.host}:{offer.port}, device_port={answer_port}, "
            f"SSRC={offer.ssrc:010d}", flush=True)

    def start_media(self, call_id: str) -> None:
        dialog = self.dialogs.get(call_id)
        if not dialog or (dialog.pusher and dialog.pusher.started):
            return
        if not dialog.pusher:
            dialog.pusher = PsRtpPusher(
                dialog.offer, self.args.ffmpeg, self.args.input,
                self.args.width, self.args.height, self.args.frame_rate,
                self.args.bind_ip, self.args.media_source_port)
        dialog.pusher.start()
        print(f"ACK received; PS/RTP push started: call={call_id}", flush=True)

    def stop_media(self, call_id: str) -> None:
        dialog = self.dialogs.pop(call_id, None)
        if dialog and dialog.pusher:
            dialog.pusher.stop()
            print(
                f"PS/RTP push stopped: call={call_id}, packets={dialog.pusher.packet_count}",
                flush=True)

    def handle_request(self, request: SipMessage) -> None:
        method = request.method.upper()
        if method == "MESSAGE" and "<CmdType>Catalog</CmdType>" in request.body.decode("utf-8", errors="replace"):
            self.handle_catalog_query(request)
        elif method == "INVITE":
            self.handle_invite(request)
        elif method == "ACK":
            self.start_media(request.header("Call-ID"))
        elif method == "BYE":
            self.send_response(self.response(request, 200, "OK"))
            self.stop_media(request.header("Call-ID"))
        elif method == "OPTIONS":
            self.send_response(self.response(request, 200, "OK"))
        else:
            self.send_response(self.response(request, 405, "Method Not Allowed"))

    def handle_message(self, message: SipMessage) -> None:
        if not message.is_response:
            self.handle_request(message)

    def run(self) -> None:
        self.register(self.args.register_expires)
        self.send_keepalive()
        self.send_catalog()
        next_heartbeat = time.monotonic() + self.args.heartbeat_interval
        next_refresh = time.monotonic() + max(1.0, self.args.register_expires * 0.8)
        deadline = time.monotonic() + self.args.run_seconds if self.args.run_seconds else None
        try:
            while self.running and (deadline is None or time.monotonic() < deadline):
                now = time.monotonic()
                if now >= next_heartbeat:
                    self.send_keepalive()
                    next_heartbeat = now + self.args.heartbeat_interval
                if now >= next_refresh:
                    self.register(self.args.register_expires)
                    next_refresh = now + max(1.0, self.args.register_expires * 0.8)
                timeout = min(0.5, max(0.01, next_heartbeat - now), max(0.01, next_refresh - now))
                self.socket.settimeout(timeout)
                try:
                    wire, _ = self.socket.recvfrom(1024 * 1024)
                except socket.timeout:
                    continue
                self.handle_message(SipMessage.parse(wire))
        finally:
            for call_id in list(self.dialogs):
                self.stop_media(call_id)
            if self.challenge:
                try:
                    self.register(0)
                except (OSError, TimeoutError, RuntimeError):
                    pass
            self.socket.close()

    def stop(self) -> None:
        self.running = False


def valid_gb_id(value: str) -> str:
    if len(value) != 20 or not value.isdigit():
        raise argparse.ArgumentTypeError("GB28181 IDs must contain exactly 20 decimal digits")
    return value


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Run a reproducible GB28181 device and PS-over-RTP stream")
    parser.add_argument("--platform-host", default="127.0.0.1")
    parser.add_argument("--platform-port", type=int, default=5060)
    parser.add_argument("--platform-id", type=valid_gb_id, default="34020000002000000001")
    parser.add_argument("--realm", default="3402000000")
    parser.add_argument("--bind-ip", default="0.0.0.0")
    parser.add_argument("--advertised-ip", default="127.0.0.1")
    parser.add_argument("--device-port", type=int, default=15060)
    parser.add_argument("--device-id", type=valid_gb_id, default="34020000001320000001")
    parser.add_argument("--channel-id", type=valid_gb_id, default="34020000001320000002")
    parser.add_argument("--channel-name", default="easySVA 模拟摄像机")
    parser.add_argument("--password", default="12345678")
    parser.add_argument("--register-expires", type=int, default=3600)
    parser.add_argument("--heartbeat-interval", type=float, default=30.0)
    parser.add_argument("--transaction-timeout", type=float, default=5.0)
    parser.add_argument("--run-seconds", type=float, default=0.0,
                        help="stop automatically after this many seconds; zero runs until Ctrl+C")
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument("--input", type=Path,
                        help="loop this input video instead of the generated test pattern")
    parser.add_argument("--width", type=int, default=640)
    parser.add_argument("--height", type=int, default=360)
    parser.add_argument("--frame-rate", type=int, default=25)
    parser.add_argument("--media-source-port", type=int, default=30000,
                        help="device TCP media listener port; zero selects a free port")
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = build_parser().parse_args(argv)
    if len(args.realm) != 10 or not args.realm.isdigit():
        raise SystemExit("--realm must contain exactly 10 decimal digits")
    if args.register_expires < 1 or args.heartbeat_interval <= 0:
        raise SystemExit("registration and heartbeat intervals must be positive")
    if args.input and not args.input.is_file():
        raise SystemExit(f"input video does not exist: {args.input}")
    simulator = GbDeviceSimulator(args)
    signal.signal(signal.SIGINT, lambda _signum, _frame: simulator.stop())
    signal.signal(signal.SIGTERM, lambda _signum, _frame: simulator.stop())
    simulator.run()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
