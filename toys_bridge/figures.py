"""Figure library for the toys-to-life bridge.

Scans and validates figure dumps for the three supported toy systems:

  skylanders  MIFARE Classic 1K images, 0x400 bytes (64 blocks x 16 bytes).
              Byte layout follows the dump format used by Dolphin's and
              RPCS3's emulated portals: bytes 0-3 UID, byte 4 BCC
              (UID0^UID1^UID2^UID3), figure id u16 LE at 0x10, variant id
              u16 LE at 0x1C.
  infinity    Disney Infinity token dumps: raw tag pages (4 bytes each),
              7-byte UID at bytes 0-6. Sizes between 0x40 and 0x400 bytes
              (multiple of 4) are accepted.
  amiibo      NTAG215 images, 540 bytes (135 pages x 4 bytes). BCC0 at
              byte 3 (= 0x88 ^ UID0 ^ UID1 ^ UID2), BCC1 at byte 8
              (= UID3 ^ UID4 ^ UID5 ^ UID6), amiibo id (8 bytes) at 0x54.

The library never generates figure data from nothing: it manages dumps the
user owns. Display names come from the optional CSV files
figures/<kind>_names.csv (user-extensible; the raw ids are always shown).
"""

from __future__ import annotations

import csv
import os
from dataclasses import dataclass, field

SKYLANDERS_SIZE = 0x400
SKYLANDERS_BLOCKS = 64
AMIIBO_SIZE = 540
AMIIBO_PAGES = 135

KINDS = ("skylanders", "infinity", "amiibo")

_DUMP_EXTS = {".bin", ".sky", ".dmp", ".dump", ".nfc", ".mfc", ".fig"}


@dataclass
class FigureInfo:
    kind: str
    path: str
    size: int
    valid: bool
    uid: bytes = b""
    figure_id: int = -1
    variant_id: int = -1
    name: str = ""
    notes: list = field(default_factory=list)

    @property
    def uid_hex(self) -> str:
        return self.uid.hex()

    @property
    def label(self) -> str:
        base = self.name or os.path.splitext(os.path.basename(self.path))[0]
        if self.figure_id >= 0:
            return "%s (id %d)" % (base, self.figure_id)
        return base


def _load_name_db(figures_root: str, kind: str) -> dict:
    """Load figures/<kind>_names.csv -> {key: name}.

    skylanders: key is the decimal figure id. amiibo: key is the lowercase
    amiibo id hex prefix. infinity: key is the lowercase uid hex prefix.
    """
    db = {}
    path = os.path.join(figures_root, "%s_names.csv" % kind)
    if not os.path.isfile(path):
        return db
    try:
        with open(path, newline="", encoding="utf-8") as handle:
            for row in csv.reader(handle):
                if len(row) >= 2 and row[0].strip() and not row[0].startswith("#"):
                    db[row[0].strip().lower()] = row[1].strip()
    except OSError:
        pass
    return db


def validate_skylanders(data: bytes, names: dict) -> FigureInfo:
    info = FigureInfo(kind="skylanders", path="", size=len(data), valid=False)
    if len(data) != SKYLANDERS_SIZE:
        info.notes.append("expected 0x400 bytes, got 0x%X" % len(data))
        return info
    info.uid = bytes(data[0:4])
    bcc = data[4]
    want_bcc = data[0] ^ data[1] ^ data[2] ^ data[3]
    if bcc != want_bcc:
        info.notes.append("BCC mismatch: 0x%02X != 0x%02X" % (bcc, want_bcc))
    info.figure_id = data[0x10] | (data[0x11] << 8)
    info.variant_id = data[0x1C] | (data[0x1D] << 8)
    info.name = names.get(str(info.figure_id), "")
    info.valid = not info.notes
    return info


def validate_amiibo(data: bytes, names: dict) -> FigureInfo:
    info = FigureInfo(kind="amiibo", path="", size=len(data), valid=False)
    if len(data) != AMIIBO_SIZE:
        info.notes.append("expected 540 bytes, got %d" % len(data))
        return info
    info.uid = bytes(data[0:3]) + bytes(data[4:8])
    bcc0 = data[3]
    want0 = 0x88 ^ data[0] ^ data[1] ^ data[2]
    if bcc0 != want0:
        info.notes.append("BCC0 mismatch: 0x%02X != 0x%02X" % (bcc0, want0))
    bcc1 = data[8]
    want1 = data[4] ^ data[5] ^ data[6] ^ data[7]
    if bcc1 != want1:
        info.notes.append("BCC1 mismatch: 0x%02X != 0x%02X" % (bcc1, want1))
    amiibo_id = bytes(data[0x54:0x5C])
    info.figure_id = (amiibo_id[0] << 8) | amiibo_id[1]
    id_hex = amiibo_id.hex()
    for length in (16, 14, 12, 10, 8, 6, 4):
        hit = names.get(id_hex[:length])
        if hit:
            info.name = hit
            break
    info.valid = not info.notes
    return info


def validate_infinity(data: bytes, names: dict) -> FigureInfo:
    info = FigureInfo(kind="infinity", path="", size=len(data), valid=False)
    if len(data) < 0x40 or len(data) > 0x400 or len(data) % 4:
        info.notes.append(
            "expected a multiple of 4 bytes in 0x40..0x400, got 0x%X" % len(data))
        return info
    info.uid = bytes(data[0:7])
    info.name = names.get(info.uid.hex(), "")
    info.valid = True
    return info


_VALIDATORS = {
    "skylanders": validate_skylanders,
    "infinity": validate_infinity,
    "amiibo": validate_amiibo,
}


def inspect_figure(kind: str, path: str, figures_root: str) -> FigureInfo:
    names = _load_name_db(figures_root, kind)
    try:
        with open(path, "rb") as handle:
            data = handle.read()
    except OSError as exc:
        info = FigureInfo(kind=kind, path=path, size=0, valid=False)
        info.notes.append(str(exc))
        return info
    info = _VALIDATORS[kind](data, names)
    info.path = path
    return info


def scan_library(figures_root: str) -> dict:
    """-> {kind: [FigureInfo, ...]} for every dump under figures/<kind>/."""
    library = {}
    for kind in KINDS:
        entries = []
        folder = os.path.join(figures_root, kind)
        if os.path.isdir(folder):
            for name in sorted(os.listdir(folder)):
                if os.path.splitext(name)[1].lower() not in _DUMP_EXTS:
                    continue
                entries.append(inspect_figure(kind, os.path.join(folder, name),
                                              figures_root))
        library[kind] = entries
    return library


def find_figure(library: dict, kind: str, query: str) -> FigureInfo | None:
    """Resolve a user query (name substring, id, uid hex, or path)."""
    if os.path.isfile(query):
        return inspect_figure(kind, query, os.path.dirname(os.path.dirname(query))
                              if os.sep in query else ".")
    query_l = query.lower()
    entries = library.get(kind, [])
    for entry in entries:  # exact id / uid first
        if query_l in (str(entry.figure_id), entry.uid_hex):
            return entry
    for entry in entries:
        if query_l in entry.label.lower() or query_l in entry.path.lower():
            return entry
    return None
