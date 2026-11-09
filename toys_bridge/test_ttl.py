#!/usr/bin/env python3
"""Unit tests for the toys-to-life bridge. Run: python3 -m unittest -v"""

import json
import os
import shutil
import socket
import tempfile
import threading
import unittest

import bridge_app
from amiibo_nfc import NTAG215, bcc0, bcc1
from figures import (AMIIBO_SIZE, SKYLANDERS_SIZE, inspect_figure,
                     scan_library, validate_amiibo, validate_skylanders)
from infinity_base import InfinityBase, SLOT_HEXAGON
from skylanders_portal import SkylandersPortal


def make_skylander_image(uid=b"\x01\x02\x03\x04", figure_id=20, variant=0):
    data = bytearray(SKYLANDERS_SIZE)
    data[0:4] = uid
    data[4] = uid[0] ^ uid[1] ^ uid[2] ^ uid[3]
    data[0x10] = figure_id & 0xFF
    data[0x11] = (figure_id >> 8) & 0xFF
    data[0x1C] = variant & 0xFF
    data[0x1D] = (variant >> 8) & 0xFF
    return bytes(data)


def make_amiibo_image(uid=b"\x04\xA1\xB2\xC3\xD4\xE5\xF6"):
    data = bytearray(AMIIBO_SIZE)
    data[0:3] = uid[0:3]
    data[3] = bcc0(uid[0:3])
    data[4:8] = uid[3:7]
    data[8] = bcc1(uid[3:7])
    data[12:16] = b"\xE1\x10\x3E\x00"  # capability container
    data[0x54:0x5C] = b"\x01\x02\x00\x00\x00\x09\x00\x02"  # amiibo id
    return bytes(data)


class TestSkylandersFigure(unittest.TestCase):
    def test_valid_image(self):
        info = validate_skylanders(make_skylander_image(), {})
        self.assertTrue(info.valid, info.notes)
        self.assertEqual(info.figure_id, 20)
        self.assertEqual(info.uid_hex, "01020304")

    def test_bad_size_rejected(self):
        info = validate_skylanders(b"\x00" * 100, {})
        self.assertFalse(info.valid)

    def test_bad_bcc_flagged(self):
        data = bytearray(make_skylander_image())
        data[4] ^= 0xFF
        info = validate_skylanders(bytes(data), {})
        self.assertFalse(info.valid)


class TestAmiibo(unittest.TestCase):
    def test_valid_image(self):
        info = validate_amiibo(make_amiibo_image(), {})
        self.assertTrue(info.valid, info.notes)
        self.assertEqual(info.uid_hex, "04a1b2c3d4e5f6")
        self.assertEqual(info.figure_id, 0x0102)

    def test_bad_bcc_flagged(self):
        data = bytearray(make_amiibo_image())
        data[8] ^= 0xFF
        self.assertFalse(validate_amiibo(bytes(data), {}).valid)

    def test_page_access_and_protection(self):
        tag = NTAG215(make_amiibo_image())
        self.assertEqual(tag.read_page(0)[0], 0x04)
        self.assertFalse(tag.write_page(0, b"\xFF" * 4))  # UID pages read-only
        self.assertTrue(tag.write_page(20, b"\xAA\xBB\xCC\xDD"))
        self.assertEqual(tag.read_page(20), b"\xAA\xBB\xCC\xDD")
        self.assertTrue(tag.dirty)


class TestPortal(unittest.TestCase):
    def setUp(self):
        self.portal = SkylandersPortal()
        self.info = validate_skylanders(make_skylander_image(), {})

    def test_activate_and_status(self):
        reply = self.portal.handle_command(b"S")
        self.assertEqual(reply[0], ord("S"))
        self.assertEqual(reply[1], 0x00)  # not active yet
        self.portal.handle_command(b"A\x01")
        reply = self.portal.handle_command(b"S")
        self.assertEqual(reply[1], 0x01)

    def test_place_query_write_remove(self):
        slot = self.portal.place(self.info, make_skylander_image(), -1)
        self.assertEqual(slot, 0)
        # change counter bumped
        self.assertEqual(self.portal.handle_command(b"S")[2], 1)
        # query block 1 returns the figure id bytes
        reply = self.portal.handle_command(bytes([ord("Q"), slot, 1]))
        self.assertEqual(reply[0], ord("Q"))
        self.assertEqual(reply[3], 20)  # figure id 20 at block 1 byte 0
        # write block 4, read it back
        payload = bytes(range(16))
        self.portal.handle_command(bytes([ord("W"), slot, 4]) + payload)
        reply = self.portal.handle_command(bytes([ord("Q"), slot, 4]))
        self.assertEqual(bytes(reply[3:19]), payload)
        # block 0 is read-only
        self.portal.handle_command(bytes([ord("W"), slot, 0]) + b"\x99" * 16)
        reply = self.portal.handle_command(bytes([ord("Q"), slot, 0]))
        self.assertEqual(bytes(reply[3:7]), b"\x01\x02\x03\x04")
        # removal bumps the counter again
        self.portal.remove(slot)
        self.assertEqual(self.portal.handle_command(b"S")[2], 2)
        reply = self.portal.handle_command(bytes([ord("Q"), slot, 1]))
        self.assertEqual(bytes(reply[3:19]), bytes(16))  # empty slot -> zeros

    def test_color(self):
        self.portal.handle_command(bytes([ord("C"), 10, 20, 30]))
        self.assertEqual(self.portal.color, (10, 20, 30))

    def test_report_size(self):
        for frame in (b"S", b"A\x01", b"R", b"C\x01\x02\x03",
                      bytes([ord("Q"), 0, 0]), bytes([ord("W"), 0, 1]) + bytes(16)):
            self.assertEqual(len(self.portal.handle_command(frame)), 0x20)


class TestInfinityBase(unittest.TestCase):
    def test_place_read_write_remove(self):
        base = InfinityBase()
        data = bytes(range(256)) * 2  # 0x200 bytes, uid = 00..06
        info = validate_skylanders  # not used; build a minimal stand-in

        class Info:
            path = ""
            label = "test token"
            uid_hex = "00010203040506"

        slot = base.place(Info(), data, -1)
        self.assertEqual(base.change_counter, 1)
        self.assertEqual(base.read_pages(slot, 0, 2), data[0:8])
        self.assertTrue(base.write_page(slot, 4, b"\xDE\xAD\xBE\xEF"))
        self.assertEqual(base.read_pages(slot, 4, 1), b"\xDE\xAD\xBE\xEF")
        base.remove(slot)
        self.assertEqual(base.change_counter, 2)
        self.assertEqual(base.read_pages(slot, 0, 2), bytes(8))
        self.assertNotIn(SLOT_HEXAGON, base.occupied_slots())


class TestNfcFolder(unittest.TestCase):
    """Dumps dropped loose in figures/nfc/ are classified by content."""

    def test_auto_classification(self):
        with tempfile.TemporaryDirectory() as tmp:
            nfc = os.path.join(tmp, "nfc")
            os.makedirs(nfc)
            with open(os.path.join(nfc, "link.bin"), "wb") as handle:
                handle.write(make_amiibo_image())
            with open(os.path.join(nfc, "spyro.bin"), "wb") as handle:
                handle.write(make_skylander_image())
            with open(os.path.join(nfc, "token.bin"), "wb") as handle:
                handle.write(bytes(range(256)))  # 0x100 -> infinity token
            library = scan_library(tmp)
            self.assertEqual(len(library["amiibo"]), 1)
            self.assertEqual(library["amiibo"][0].figure_id, 0x0102)
            self.assertEqual(len(library["skylanders"]), 1)
            self.assertEqual(library["skylanders"][0].figure_id, 20)
            self.assertEqual(len(library["infinity"]), 1)
            self.assertTrue(library["infinity"][0].valid)


class TestBridgeTcp(unittest.TestCase):
    """End-to-end: real TCP server, real figure library in a temp dir."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix="ttl_test_")
        for kind in ("skylanders", "infinity", "amiibo"):
            os.makedirs(os.path.join(cls.tmp, "figures", kind))
        with open(os.path.join(cls.tmp, "figures", "skylanders", "spyro.bin"),
                  "wb") as handle:
            handle.write(make_skylander_image())
        with open(os.path.join(cls.tmp, "figures", "amiibo", "link.bin"),
                  "wb") as handle:
            handle.write(make_amiibo_image())
        cls._old_figures = bridge_app.FIGURES_DIR
        cls._old_active = bridge_app.ACTIVE_DIR
        bridge_app.FIGURES_DIR = os.path.join(cls.tmp, "figures")
        bridge_app.ACTIVE_DIR = os.path.join(cls.tmp, "active")
        cls.state = bridge_app.BridgeState()
        cls.server = bridge_app.serve(cls.state, 0)  # ephemeral port
        cls.port = cls.server.server_address[1]
        cls.thread = threading.Thread(target=cls.server.serve_forever,
                                      daemon=True)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        bridge_app.FIGURES_DIR = cls._old_figures
        bridge_app.ACTIVE_DIR = cls._old_active
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def rpc(self, request: dict) -> dict:
        with socket.create_connection(("127.0.0.1", self.port), timeout=5) as sock:
            sock.sendall((json.dumps(request) + "\n").encode())
            with sock.makefile("rb") as reader:
                return json.loads(reader.readline().decode())

    def test_hello(self):
        reply = self.rpc({"cmd": "hello"})
        self.assertTrue(reply["ok"])
        self.assertEqual(reply["app"], "omnicon-toys-bridge")

    def test_game_announce_and_state(self):
        self.assertTrue(self.rpc({"cmd": "game", "system": "wii",
                                  "title": "Skylanders Spyro's Adventure"})["ok"])
        state = self.rpc({"cmd": "state"})
        self.assertEqual(state["game"]["system"], "wii")

    def test_place_and_portal_protocol_over_tcp(self):
        reply = self.rpc({"cmd": "place", "kind": "skylanders",
                          "figure": "spyro"})
        self.assertTrue(reply["ok"], reply)
        # raw portal frame: query block 1 of slot 0
        frame = bytes([ord("Q"), 0, 1]) + bytes(29)
        reply = self.rpc({"cmd": "portal_command", "data": frame.hex()})
        self.assertTrue(reply["ok"])
        data = bytes.fromhex(reply["data"])
        self.assertEqual(data[0], ord("Q"))
        self.assertEqual(data[3], 20)

    def test_amiibo_place_and_stage(self):
        self.assertTrue(self.rpc({"cmd": "place", "kind": "skylanders",
                                  "figure": "spyro"})["ok"])
        self.assertTrue(self.rpc({"cmd": "place", "kind": "amiibo",
                                  "figure": "link"})["ok"])
        reply = self.rpc({"cmd": "stage", "system": "wiiu"})
        self.assertTrue(reply["ok"])
        self.assertTrue(reply["figures"])
        for path in reply["figures"]:
            self.assertTrue(os.path.isfile(path), path)
        names = [os.path.basename(p) for p in reply["figures"]]
        self.assertTrue(any("skylanders" in n for n in names))
        self.assertTrue(any("amiibo" in n for n in names))

    def test_unknown_command(self):
        self.assertFalse(self.rpc({"cmd": "bogus"})["ok"])


if __name__ == "__main__":
    unittest.main()
