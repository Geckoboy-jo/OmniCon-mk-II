#!/usr/bin/env python3
"""OmniCon toys-to-life bridge app.

A separate process that simulates toys-to-life hardware - the Skylanders
Portal of Power, the Disney Infinity base, and amiibo (NTAG215) tags - and
lets OmniCon (or any other app) drive it over a JSON-lines TCP API.

Usage:
  python3 bridge_app.py             interactive terminal UI + TCP server
  python3 bridge_app.py --headless  TCP server only (spawned by OmniCon)

TCP API (default 127.0.0.1:8399, override with OMNICON_TTL_PORT), one JSON
object per line:
  {"cmd":"hello"}                                  handshake
  {"cmd":"game","system":"wii","title":"..."}      announce a launching game
  {"cmd":"list","kind":"skylanders|infinity|amiibo"}
  {"cmd":"state"}                                  full device state
  {"cmd":"place","kind":...,"figure":"spyro|id|path","slot":-1}
  {"cmd":"remove","kind":...,"slot":0}
  {"cmd":"color","r":255,"g":120,"b":0}            portal LED
  {"cmd":"stage","system":"wii"}                   copy active figures to
                                                   toys_bridge/active/<system>/
  {"cmd":"save"}                                   persist game-written figures
  {"cmd":"portal_command","data":"<64 hex chars>"} raw Portal of Power frame
  {"cmd":"quit"}

Figure dumps live in toys_bridge/figures/<kind>/ and are files you own;
the app validates and manages them but never fabricates figure data.
"""

from __future__ import annotations

import json
import os
import shutil
import socketserver
import sys
import threading

from amiibo_nfc import NTAG215
from figures import KINDS, find_figure, inspect_figure, scan_library
from infinity_base import InfinityBase, SLOT_NAMES
from skylanders_portal import SkylandersPortal

APP_NAME = "omnicon-toys-bridge"
APP_VERSION = 1
DEFAULT_PORT = 8399

BRIDGE_DIR = os.path.dirname(os.path.abspath(__file__))
FIGURES_DIR = os.path.join(BRIDGE_DIR, "figures")
ACTIVE_DIR = os.path.join(BRIDGE_DIR, "active")


class BridgeState:
    """All simulated hardware plus the figure library."""

    def __init__(self):
        self.lock = threading.Lock()
        self.portal = SkylandersPortal()
        self.base = InfinityBase()
        self.amiibo: NTAG215 | None = None
        self.amiibo_info = None
        self.game = None  # {"system":..., "title":...} announced by OmniCon
        self.library = scan_library(FIGURES_DIR)

    def rescan(self):
        self.library = scan_library(FIGURES_DIR)

    # -- placement ----------------------------------------------------------

    def place(self, kind: str, query: str, slot: int = -1) -> dict:
        info = find_figure(self.library, kind, query)
        if info is None:
            return {"ok": False, "error": "no %s figure matches %r" % (kind, query)}
        if not info.valid:
            return {"ok": False,
                    "error": "invalid dump: %s" % "; ".join(info.notes)}
        with open(info.path, "rb") as handle:
            data = handle.read()
        if kind == "skylanders":
            slot = self.portal.place(info, data, slot)
        elif kind == "infinity":
            slot = self.base.place(info, data, slot)
        else:
            self.amiibo = NTAG215(data, info.path)
            self.amiibo_info = info
            slot = 0
        return {"ok": True, "slot": slot, "figure": info.label}

    def remove(self, kind: str, slot: int) -> dict:
        if kind == "skylanders":
            self.portal.remove(slot)
        elif kind == "infinity":
            self.base.remove(slot)
        else:
            self.amiibo = None
            self.amiibo_info = None
        return {"ok": True}

    # -- staging ------------------------------------------------------------

    def stage(self, system: str) -> dict:
        """Copy the active figures into active/<system>/ for the emulator."""
        target = os.path.join(ACTIVE_DIR, system)
        os.makedirs(target, exist_ok=True)
        for stale in os.listdir(target):
            os.unlink(os.path.join(target, stale))
        staged = []
        for slot in self.portal.occupied_slots():
            entry = self.portal.figure_at(slot)
            staged.append(self._write_staged(target, "skylanders", slot, entry))
        for slot in self.base.occupied_slots():
            entry = self.base.figure_at(slot)
            staged.append(self._write_staged(target, "infinity", slot, entry))
        if self.amiibo is not None:
            staged.append(self._write_staged(
                target, "amiibo", 0,
                {"info": self.amiibo_info, "data": self.amiibo.image()}))
        return {"ok": True, "dir": target, "figures": staged}

    @staticmethod
    def _write_staged(target: str, kind: str, slot: int, entry: dict) -> str:
        info = entry["info"]
        base = os.path.basename(info.path) if info and info.path else ""
        if not base:
            base = "%s_slot%d.bin" % (kind, slot)
        name = "%s_slot%d_%s" % (kind, slot, base)
        path = os.path.join(target, name)
        data = entry["data"]
        if isinstance(data, bytearray):
            data = bytes(data)
        if callable(data):  # NTAG215.image() was passed bound
            data = data()
        with open(path, "wb") as handle:
            handle.write(data)
        return path

    # -- state dump ---------------------------------------------------------

    def state(self) -> dict:
        with self.lock:
            portal_slots = []
            for slot in self.portal.occupied_slots():
                entry = self.portal.figure_at(slot)
                portal_slots.append({"slot": slot,
                                     "figure": entry["info"].label,
                                     "uid": entry["info"].uid_hex})
            base_slots = []
            for slot in self.base.occupied_slots():
                entry = self.base.figure_at(slot)
                base_slots.append({"slot": slot, "pad": SLOT_NAMES[slot],
                                   "figure": entry["info"].label,
                                   "uid": entry["info"].uid_hex})
            return {
                "ok": True,
                "game": self.game,
                "portal": {"active": self.portal.active,
                           "color": list(self.portal.color),
                           "figures": portal_slots},
                "infinity": {"figures": base_slots},
                "amiibo": ({"figure": self.amiibo_info.label,
                            "uid": self.amiibo.uid.hex()}
                           if self.amiibo is not None else None),
            }


def _handle_request(state: BridgeState, request: dict) -> dict:
    cmd = request.get("cmd", "")
    if cmd == "hello":
        return {"ok": True, "app": APP_NAME, "version": APP_VERSION}
    if cmd == "game":
        state.game = {"system": request.get("system", ""),
                      "title": request.get("title", "")}
        return {"ok": True}
    if cmd == "list":
        kind = request.get("kind")
        kinds = [kind] if kind in KINDS else KINDS
        out = {}
        for k in kinds:
            out[k] = [{"label": e.label, "path": e.path, "valid": e.valid,
                       "uid": e.uid_hex, "figure_id": e.figure_id,
                       "notes": e.notes} for e in state.library.get(k, [])]
        return {"ok": True, "library": out}
    if cmd == "rescan":
        state.rescan()
        return {"ok": True}
    if cmd == "state":
        return state.state()
    if cmd == "place":
        with state.lock:
            return state.place(request.get("kind", ""),
                               str(request.get("figure", "")),
                               int(request.get("slot", -1)))
    if cmd == "remove":
        with state.lock:
            return state.remove(request.get("kind", ""),
                                int(request.get("slot", 0)))
    if cmd == "color":
        state.portal.color = (int(request.get("r", 0)), int(request.get("g", 0)),
                              int(request.get("b", 0)))
        return {"ok": True, "color": list(state.portal.color)}
    if cmd == "stage":
        with state.lock:
            return state.stage(request.get("system", "misc"))
    if cmd == "save":
        with state.lock:
            saved = state.portal.save_figures() + state.base.save_figures()
            if state.amiibo is not None and state.amiibo.dirty:
                saved.append(state.amiibo.save())
        return {"ok": True, "saved": saved}
    if cmd == "portal_command":
        try:
            frame = bytes.fromhex(request.get("data", ""))
        except ValueError:
            return {"ok": False, "error": "data must be hex"}
        with state.lock:
            reply = state.portal.handle_command(frame)
        return {"ok": True, "data": reply.hex()}
    if cmd == "quit":
        return {"ok": True, "quit": True}
    return {"ok": False, "error": "unknown cmd %r" % cmd}


class _Handler(socketserver.StreamRequestHandler):
    def handle(self):
        for line in self.rfile:
            try:
                request = json.loads(line.decode("utf-8", "replace"))
            except ValueError:
                reply = {"ok": False, "error": "bad json"}
            else:
                try:
                    reply = _handle_request(self.server.state, request)
                except Exception as exc:  # keep the bridge alive on bad input
                    reply = {"ok": False, "error": str(exc)}
            self.wfile.write((json.dumps(reply) + "\n").encode())
            if reply.get("quit"):
                threading.Thread(target=self.server.shutdown,
                                 daemon=True).start()
                return


class _Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def serve(state: BridgeState, port: int) -> _Server:
    server = _Server(("127.0.0.1", port), _Handler)
    server.state = state
    return server


# ---- interactive terminal UI ------------------------------------------------

_HELP = """commands:
  list [skylanders|infinity|amiibo]   show the figure library
  place <kind> <figure> [slot]        put a figure on the portal/base
                                      (<figure> = name, id, uid, or path)
  remove <kind> <slot>                take a figure off
  color <r> <g> <b>                   set the portal LED color
  state                               show the simulated hardware
  stage <system>                      copy active figures to active/<system>/
  save                                write game-modified figures back
  rescan                              re-read the figures/ folders
  quit
"""


def _print_state(state: BridgeState):
    snapshot = state.state()
    game = snapshot["game"]
    if game:
        print("game:    %s [%s]" % (game["title"], game["system"]))
    portal = snapshot["portal"]
    print("portal:  %s, LED #%02X%02X%02X"
          % ("active" if portal["active"] else "inactive", *portal["color"]))
    for fig in portal["figures"]:
        print("  slot %d: %s [%s]" % (fig["slot"], fig["figure"], fig["uid"]))
    for fig in snapshot["infinity"]["figures"]:
        print("  base %s: %s [%s]" % (fig["pad"], fig["figure"], fig["uid"]))
    amiibo = snapshot["amiibo"]
    print("amiibo:  %s" % (amiibo["figure"] if amiibo else "(none)"))


def repl(state: BridgeState):
    print(_HELP)
    while True:
        try:
            line = input("ttl> ").strip()
        except (EOFError, KeyboardInterrupt):
            print()
            break
        if not line:
            continue
        parts = line.split()
        verb, args = parts[0].lower(), parts[1:]
        try:
            if verb == "list":
                kind = args[0] if args else None
                reply = _handle_request(state, {"cmd": "list", "kind": kind})
                for k, entries in reply["library"].items():
                    print("%s:" % k)
                    for e in entries:
                        flag = "" if e["valid"] else "  INVALID: %s" % "; ".join(e["notes"])
                        print("  %s  [%s]%s" % (e["label"], e["uid"], flag))
            elif verb == "place" and len(args) >= 2:
                slot = int(args[2]) if len(args) > 2 else -1
                reply = _handle_request(state, {"cmd": "place", "kind": args[0],
                                                "figure": args[1], "slot": slot})
                print(reply if not reply.get("ok") else
                      "placed %s on slot %d" % (reply["figure"], reply["slot"]))
            elif verb == "remove" and len(args) >= 2:
                print(_handle_request(state, {"cmd": "remove", "kind": args[0],
                                              "slot": int(args[1])}))
            elif verb == "color" and len(args) == 3:
                print(_handle_request(state, {"cmd": "color", "r": int(args[0]),
                                              "g": int(args[1]), "b": int(args[2])}))
            elif verb == "state":
                _print_state(state)
            elif verb == "stage" and args:
                print(_handle_request(state, {"cmd": "stage", "system": args[0]}))
            elif verb == "save":
                print(_handle_request(state, {"cmd": "save"}))
            elif verb == "rescan":
                print(_handle_request(state, {"cmd": "rescan"}))
            elif verb in ("quit", "exit"):
                break
            else:
                print(_HELP)
        except Exception as exc:
            print("error: %s" % exc)


def main(argv):
    headless = "--headless" in argv
    port = int(os.environ.get("OMNICON_TTL_PORT", DEFAULT_PORT))
    state = BridgeState()
    server = serve(state, port)
    print("[%s] listening on 127.0.0.1:%d (%s)"
          % (APP_NAME, port, "headless" if headless else "interactive"))
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    if headless:
        try:
            thread.join()
        except KeyboardInterrupt:
            pass
    else:
        repl(state)
        server.shutdown()
    server.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
