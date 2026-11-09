"""Emulated Disney Infinity base.

The Infinity base has three pads: one hexagonal (play sets) and two round
(figures / power discs). Tokens are NFC tags with a 7-byte UID; the bridge
works with raw page dumps (4 bytes per page).

This class models the base's slot/presence behaviour and page-level tag
access for the bridge's TCP API. For actual gameplay, point the emulator's
own Infinity Base emulation (e.g. RPCS3: I/O -> emulated USB devices) at the
figure files this app stages into toys_bridge/active/<system>/; the wire
level of the real base involves a challenge/response exchange that the
emulator implements itself.
"""

from __future__ import annotations

PAGE_SIZE = 4

SLOT_HEXAGON = 0
SLOT_ROUND_LEFT = 1
SLOT_ROUND_RIGHT = 2
SLOT_NAMES = {SLOT_HEXAGON: "hexagon", SLOT_ROUND_LEFT: "round-left",
              SLOT_ROUND_RIGHT: "round-right"}


class InfinityBase:
    def __init__(self):
        self._slots: dict = {}  # slot -> dict(info=, data=bytearray, dirty=bool)
        self._change_counter = 0

    def place(self, info, data: bytes, slot: int) -> int:
        if slot < 0:
            for candidate in (SLOT_ROUND_LEFT, SLOT_ROUND_RIGHT, SLOT_HEXAGON):
                if candidate not in self._slots:
                    slot = candidate
                    break
            else:
                raise ValueError("base is full")
        if slot not in SLOT_NAMES:
            raise ValueError("slot out of range")
        self._slots[slot] = {"info": info, "data": bytearray(data), "dirty": False}
        self._change_counter = (self._change_counter + 1) & 0xFF
        return slot

    def remove(self, slot: int):
        if slot in self._slots:
            del self._slots[slot]
            self._change_counter = (self._change_counter + 1) & 0xFF

    def figure_at(self, slot: int):
        return self._slots.get(slot)

    def occupied_slots(self) -> list:
        return sorted(self._slots)

    @property
    def change_counter(self) -> int:
        return self._change_counter

    def read_pages(self, slot: int, first: int, count: int) -> bytes:
        entry = self._slots.get(slot)
        if entry is None:
            return bytes(count * PAGE_SIZE)
        start = first * PAGE_SIZE
        end = start + count * PAGE_SIZE
        chunk = bytes(entry["data"][start:end])
        return chunk.ljust(count * PAGE_SIZE, b"\x00")

    def write_page(self, slot: int, page: int, data: bytes) -> bool:
        entry = self._slots.get(slot)
        if entry is None or len(data) != PAGE_SIZE:
            return False
        start = page * PAGE_SIZE
        if start + PAGE_SIZE > len(entry["data"]):
            return False
        entry["data"][start:start + PAGE_SIZE] = data
        entry["dirty"] = True
        return True

    def save_figures(self) -> list:
        saved = []
        for entry in self._slots.values():
            if entry["dirty"] and entry["info"].path:
                with open(entry["info"].path, "wb") as handle:
                    handle.write(bytes(entry["data"]))
                entry["dirty"] = False
                saved.append(entry["info"].path)
        return saved
