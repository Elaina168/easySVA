#!/usr/bin/env python3
"""Unit tests for the standalone GB28181 device simulator."""

import importlib.util
import struct
import sys
import unittest
from pathlib import Path


MODULE_PATH = Path(__file__).parents[1] / "tools" / "gb28181_device_simulator.py"
SPEC = importlib.util.spec_from_file_location("gb28181_device_simulator", MODULE_PATH)
assert SPEC and SPEC.loader
simulator = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = simulator
SPEC.loader.exec_module(simulator)


class SipMessageTests(unittest.TestCase):
    def test_utf8_body_length_and_round_trip(self):
        message = simulator.SipMessage(
            "MESSAGE sip:platform@example SIP/2.0",
            [("Via", "SIP/2.0/UDP 127.0.0.1:15060"), ("Content-Type", "text/plain")],
            "模拟摄像机".encode("utf-8"),
        )
        wire = message.serialize()
        self.assertIn(b"Content-Length: 15\r\n", wire)
        parsed = simulator.SipMessage.parse(wire)
        self.assertEqual(parsed.method, "MESSAGE")
        self.assertEqual(parsed.body.decode("utf-8"), "模拟摄像机")

    def test_digest_auth_matches_rfc_style_calculation(self):
        challenge = (
            'Digest realm="3402000000", nonce="abcdef", '
            'algorithm=MD5, qop="auth"'
        )
        authorization = simulator.digest_authorization(
            challenge,
            "34020000001320000001",
            "12345678",
            "REGISTER",
            "sip:34020000002000000001@3402000000",
            1,
            "fixed-cnonce",
        )
        parameters = simulator.parse_auth_parameters(authorization)
        ha1 = simulator.md5_hex("34020000001320000001:3402000000:12345678")
        ha2 = simulator.md5_hex(
            "REGISTER:sip:34020000002000000001@3402000000")
        expected = simulator.md5_hex(
            f"{ha1}:abcdef:00000001:fixed-cnonce:auth:{ha2}")
        self.assertEqual(parameters["response"], expected)


class SdpAndMediaTests(unittest.TestCase):
    def test_parse_udp_ps_offer(self):
        offer = simulator.parse_sdp_offer(
            b"v=0\r\nc=IN IP4 127.0.0.1\r\n"
            b"m=video 30000 RTP/AVP 96\r\n"
            b"a=rtpmap:96 PS/90000\r\ny=0100000001\r\n")
        self.assertEqual(offer.host, "127.0.0.1")
        self.assertEqual(offer.port, 30000)
        self.assertEqual(offer.ssrc, 100000001)
        self.assertEqual(offer.payload_type, 96)
        self.assertFalse(offer.tcp)

    def test_parse_tcp_active_offer(self):
        offer = simulator.parse_sdp_offer(
            b"v=0\r\nc=IN IP4 127.0.0.1\r\n"
            b"m=video 30000 TCP/RTP/AVP 96\r\n"
            b"a=setup:active\r\na=connection:new\r\n"
            b"a=rtpmap:96 PS/90000\r\ny=0100000001\r\n")
        self.assertTrue(offer.tcp)
        self.assertEqual(offer.setup, "active")

    def test_tcp_rtp_uses_rfc4571_length_prefix(self):
        packet = b"rtp-packet"
        framed = simulator.frame_tcp_rtp(packet)
        self.assertEqual(struct.unpack("!H", framed[:2])[0], len(packet))
        self.assertEqual(framed[2:], packet)

    def test_ps_pack_split_survives_chunk_boundaries(self):
        pack1 = simulator.PS_PACK_START + b"one"
        pack2 = simulator.PS_PACK_START + b"two"
        pack3 = simulator.PS_PACK_START + b"three"
        stream = pack1 + pack2 + pack3
        chunks = [stream[:2], stream[2:9], stream[9:14], stream[14:]]
        self.assertEqual(list(simulator.iter_ps_packs(chunks)), [pack1, pack2, pack3])

    def test_rtp_packetization_sets_sequence_ssrc_and_last_marker(self):
        packets = list(simulator.packetize_rtp(
            b"abcdefghij", 96, 65535, 90000, 100000001, maximum_payload=4))
        self.assertEqual(len(packets), 3)
        first = struct.unpack("!BBHII", packets[0][0][:12])
        second = struct.unpack("!BBHII", packets[1][0][:12])
        last = struct.unpack("!BBHII", packets[2][0][:12])
        self.assertEqual(first, (0x80, 96, 65535, 90000, 100000001))
        self.assertEqual(second[2], 0)
        self.assertEqual(last[1], 0x80 | 96)
        self.assertEqual(packets[-1][1], 2)
        self.assertEqual(
            b"".join(packet[12:] for packet, _ in packets), b"abcdefghij")


if __name__ == "__main__":
    unittest.main()
