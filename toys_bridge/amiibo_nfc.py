"""NTAG215 tag emulation for amiibo figures.

An amiibo dump is a raw 540-byte NTAG215 image (135 pages x 4 bytes):

  pages 0-1   7-byte UID (bytes 0-2, 4-7) with BCC0 at byte 3, BCC1 at byte 8
  page  2     lock bytes (read-only)
  page  3     capability container (read-only)
  pages 4-129 user data; the 8-byte amiibo id lives at offset 0x54 (page 21)
  pages 130-134 dynamic lock / config / PWD / PACK

Emulators such as Cemu, Citra and Ryujinx consume the raw .bin image; this
class provides validated page-level access so the bridge can serve reads and
writes exactly like the physical tag, and persist game-written data.
"""

from __future__ import annotations

PAGE_SIZE = 4
PAGES = 135
TAG_SIZE = PAGES * PAGE_SIZE
READ_ONLY_PAGES = frozenset((0, 1, 2, 3))
AMIIBO_ID_OFFSET = 0x54


class NTAG215:
    def __init__(self, data: bytes, path: str = ""):
        if len(data) != TAG_SIZE:
            raise ValueError("NTAG215 image must be %d bytes" % TAG_SIZE)
        self._data = bytearray(data)
        self.path = path
        self.dirty = False

    @property
    def uid(self) -> bytes:
        return bytes(self._data[0:3]) + bytes(self._data[4:8])

    @property
    def amiibo_id(self) -> bytes:
        return bytes(self._data[AMIIBO_ID_OFFSET:AMIIBO_ID_OFFSET + 8])

    def read_page(self, page: int) -> bytes:
        if not 0 <= page < PAGES:
            raise ValueError("page out of range")
        start = page * PAGE_SIZE
        return bytes(self._data[start:start + PAGE_SIZE])

    def read_pages(self, first: int, count: int) -> bytes:
        return b"".join(self.read_page(first + i) for i in range(count))

    def write_page(self, page: int, data: bytes) -> bool:
        if not 0 <= page < PAGES or len(data) != PAGE_SIZE:
            raise ValueError("bad page write")
        if page in READ_ONLY_PAGES:
            return False
        start = page * PAGE_SIZE
        self._data[start:start + PAGE_SIZE] = data
        self.dirty = True
        return True

    def save(self, path: str = "") -> str:
        target = path or self.path
        if not target:
            raise ValueError("no path to save to")
        with open(target, "wb") as handle:
            handle.write(bytes(self._data))
        self.dirty = False
        return target

    def image(self) -> bytes:
        return bytes(self._data)


def bcc0(uid_first3: bytes) -> int:
    return 0x88 ^ uid_first3[0] ^ uid_first3[1] ^ uid_first3[2]


def bcc1(uid_last4: bytes) -> int:
    return uid_last4[0] ^ uid_last4[1] ^ uid_last4[2] ^ uid_last4[3]
