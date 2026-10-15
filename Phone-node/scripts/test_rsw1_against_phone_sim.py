#!/usr/bin/env python3
"""
Test harness verifying RSW1 binary encoding fidelity against phone_sim.py
and dev_version/rideshield-core/lib/RideShieldCore/src/phone.*
"""

import os
import struct
import sys
import unittest

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '../../dev_version/rideshield-core/tools')))
from phone_sim import packet, WIRE, DEFAULT_TOKEN

class TestRsw1WireLayout(unittest.TestCase):

    def test_golden_vector(self):
        # Golden vector from test_tools.py:
        # packet(4, 42, 3, 2010, source=1, flags=11, hazards=1, confidence=90)
        b = packet(4, 42, 3, 2010, source=1, flags=11, hazards=1, confidence=90)
        expected_hex = '5253573104010b012a00000003000000da0700000000000000000000005a0000' + '00' * 32
        self.assertEqual(b.hex(), expected_hex)
        self.assertEqual(len(b), 64)

    def test_auth_packet_layout(self):
        token = b'bench-token-change-before-use-01'
        b = packet(1, session=100, seq=1, token=token)
        self.assertEqual(len(b), 64)
        magic, kind, src, flags, hazards, session, seq, phone_ms, aux, zone, conf, res, tok = WIRE.unpack(b)
        self.assertEqual(magic, b'RSW1')
        self.assertEqual(kind, 1)
        self.assertEqual(src, 0)
        self.assertEqual(flags, 0)
        self.assertEqual(hazards, 0)
        self.assertEqual(session, 100)
        self.assertEqual(seq, 1)
        self.assertEqual(phone_ms, 0)
        self.assertEqual(aux, 0)
        self.assertEqual(zone, 0)
        self.assertEqual(conf, 0)
        self.assertEqual(res, 0)
        self.assertEqual(tok, token)

    def test_sync_packet_layout(self):
        b = packet(2, session=50, seq=2, phone_ms=12345, auxiliary=7)
        magic, kind, src, flags, hazards, session, seq, phone_ms, aux, zone, conf, res, tok = WIRE.unpack(b)
        self.assertEqual(kind, 2)
        self.assertEqual(session, 50)
        self.assertEqual(seq, 2)
        self.assertEqual(phone_ms, 12345)
        self.assertEqual(aux, 7)
        self.assertEqual(tok, b'\x00' * 32)

    def test_heartbeat_packet_layout(self):
        b = packet(3, session=50, seq=3, phone_ms=13345)
        magic, kind, src, flags, hazards, session, seq, phone_ms, aux, zone, conf, res, tok = WIRE.unpack(b)
        self.assertEqual(kind, 3)
        self.assertEqual(session, 50)
        self.assertEqual(seq, 3)
        self.assertEqual(phone_ms, 13345)
        self.assertEqual(aux, 0)
        self.assertEqual(tok, b'\x00' * 32)

    def test_schema_rejection_rules(self):
        # Front observation cannot have rear-vehicle hazard (bit 4)
        with self.assertRaises(ValueError):
            packet(4, 1, 1, phone_ms=1000, source=1, hazards=4)

        # Front observation cannot have non-zero zone
        with self.assertRaises(ValueError):
            packet(4, 1, 1, phone_ms=1000, source=1, zone=1)

        # Rear observation cannot have pothole or wet hazards
        with self.assertRaises(ValueError):
            packet(4, 1, 1, phone_ms=1000, source=2, hazards=1)

        # Control packet cannot have non-zero observation fields
        with self.assertRaises(ValueError):
            packet(3, 1, 1, phone_ms=1000, hazards=1)

if __name__ == '__main__':
    unittest.main()
