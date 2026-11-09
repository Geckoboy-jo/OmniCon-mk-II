"""Emulated Skylanders Portal of Power.

Implements the portal's 32-byte HID report protocol, following the command
set used by the public portal documentation and Dolphin's emulated portal:

Host -> portal (first byte is the command):
  'S'        status        -> 'S', ready, change_counter, 0...
  'A' 0/1    deactivate/activate -> 'A', ready, 0...
  'R'        reset         -> 'R', 0...  (clears the change counter)
  'Q' i b    query block b (0-63) of the figure in slot i
             -> 'Q', i, b, <16 data bytes> (zeros when the slot is empty)
  'W' i b d  write 16 bytes d to block b of the figure in slot i
             -> 'W', i, b  (block 0 is read-only, like real tags)
  'C' r g b  set the portal LED color  -> 'C', 0...
  'J' s r g b  sided color (Trap Team portals) -> 'J', 0...
  'L'/'M'    light effects, acknowledged      -> echo, 0...

The change counter in the status response increments every time a figure is
placed on or removed from the portal; games poll 'S' and re-enumerate slots
when it changes, which is how figure hot-swapping is detected.

Figure data is the raw 0x400-byte MIFARE Classic 1K image from the figure
library. Writes update the in-memory image; call save_figures() to persist
modified images back to their files (level/star progress etc.).
"""

from __future__ import annotations

REPORT_SIZE = 0x20
BLOCKS = 64
BLOCK_SIZE = 16
MAX_SLOTS = 16


class SkylandersPortal:
    def __init__(self, max_slots: int = MAX_SLOTS):
        self.max_slots = max_slots
        self.active = False
        self.color = (0, 0, 0)
        self._slots: list = [None] * max_slots  # each: dict(info=, data=bytearray, dirty=bool)
        self._change_counter = 0

    # ---- figure management -------------------------------------------------

    def place(self, info, data: bytes, slot: int) -> int:
        """Put a figure on the first free slot (or an explicit slot)."""
        if slot < 0:
            for candidate in range(self.max_slots):
                if self._slots[candidate] is None:
                    slot = candidate
                    break
            else:
                raise ValueError("portal is full")
        if not 0 <= slot < self.max_slots:
            raise ValueError("slot out of range")
        if len(data) != BLOCKS * BLOCK_SIZE:
            raise ValueError("figure image must be 0x400 bytes")
        self._slots[slot] = {"info": info, "data": bytearray(data), "dirty": False}
        self._change_counter = (self._change_counter + 1) & 0xFF
        return slot

    def remove(self, slot: int):
        if 0 <= slot < self.max_slots and self._slots[slot] is not None:
            self._slots[slot] = None
            self._change_counter = (self._change_counter + 1) & 0xFF

    def figure_at(self, slot: int):
        if 0 <= slot < self.max_slots:
            return self._slots[slot]
        return None

    def occupied_slots(self) -> list:
        return [i for i in range(self.max_slots) if self._slots[i] is not None]

    def save_figures(self) -> list:
        """Write dirty in-memory images back to their files. -> saved paths."""
        saved = []
        for entry in self._slots:
            if entry and entry["dirty"] and entry["info"].path:
                with open(entry["info"].path, "wb") as handle:
                    handle.write(bytes(entry["data"]))
                entry["dirty"] = False
                saved.append(entry["info"].path)
        return saved

    # ---- protocol ----------------------------------------------------------

    def handle_command(self, request: bytes) -> bytes:
        if not request:
            return bytes(REPORT_SIZE)
        command = request[0]
        response = bytearray(REPORT_SIZE)

        if command == ord("S"):
            response[0] = ord("S")
            response[1] = 0x01 if self.active else 0x00
            response[2] = self._change_counter
        elif command == ord("A"):
            self.active = len(request) < 2 or request[1] != 0x00
            response[0] = ord("A")
            response[1] = 0x01 if self.active else 0x00
        elif command == ord("R"):
            self._change_counter = 0
            response[0] = ord("R")
        elif command == ord("Q"):
            index = request[1] & 0x0F if len(request) > 1 else 0
            block = request[2] if len(request) > 2 else 0
            response[0] = ord("Q")
            response[1] = index
            response[2] = block
            entry = self.figure_at(index)
            if entry is not None and block < BLOCKS:
                start = block * BLOCK_SIZE
                response[3:3 + BLOCK_SIZE] = entry["data"][start:start + BLOCK_SIZE]
        elif command == ord("W"):
            index = request[1] & 0x0F if len(request) > 1 else 0
            block = request[2] if len(request) > 2 else 0
            response[0] = ord("W")
            response[1] = index
            response[2] = block
            entry = self.figure_at(index)
            if entry is not None and 0 < block < BLOCKS:
                start = block * BLOCK_SIZE
                entry["data"][start:start + BLOCK_SIZE] = request[3:3 + BLOCK_SIZE]
                entry["dirty"] = True
        elif command == ord("C"):
            self.color = tuple(request[1:4]) if len(request) >= 4 else (0, 0, 0)
            response[0] = ord("C")
        elif command == ord("J"):
            # Sided color on Trap Team portals; model as a plain color set.
            self.color = tuple(request[2:5]) if len(request) >= 5 else self.color
            response[0] = ord("J")
        elif command in (ord("L"), ord("M"), ord("Z")):
            response[0] = command
        else:
            response[0] = command
            response[1] = 0xFF
        return bytes(response)
