#!/usr/bin/env python3
"""
WSL-side uinput bridge.
Reads 13-byte packets from stdin and drives one virtual Xbox 360 gamepad per
controller index via /dev/uinput so that SDL2 sees each physical controller
as a real gamepad (player 1 = index 0, player 2 = index 1, ...).
"""
import struct
import sys

try:
    import evdev
    from evdev import UInput, AbsInfo, ecodes as e
except ImportError:
    sys.stderr.write("ERROR: evdev not installed. Run: pip install evdev\n")
    sys.exit(1)

# ── Packet format (must match xinput_server.py) ───────────────────────────────
PACKET_FMT  = struct.Struct('<BHhhhhBB')   # 13 bytes: index + payload
PACKET_SIZE = PACKET_FMT.size
MAX_PADS    = 4

# ── XInput button bitmasks → Linux evdev button codes ────────────────────────
# D-pad (0x000F) is handled separately as ABS_HAT0X/Y
BUTTON_MAP = {
    0x0010: e.BTN_START,
    0x0020: e.BTN_SELECT,
    0x0040: e.BTN_THUMBL,
    0x0080: e.BTN_THUMBR,
    0x0100: e.BTN_TL,       # Left shoulder
    0x0200: e.BTN_TR,       # Right shoulder
    0x1000: e.BTN_SOUTH,    # A
    0x2000: e.BTN_EAST,     # B
    0x4000: e.BTN_WEST,     # X
    0x8000: e.BTN_NORTH,    # Y
}

# ── Virtual device capabilities ───────────────────────────────────────────────
AXIS_SETUP = AbsInfo(value=0, min=-32768, max=32767, fuzz=16, flat=128, resolution=0)
TRIG_SETUP = AbsInfo(value=0, min=0,      max=255,   fuzz=0,  flat=0,   resolution=0)
HAT_SETUP  = AbsInfo(value=0, min=-1,     max=1,     fuzz=0,  flat=0,   resolution=0)

CAPABILITIES = {
    e.EV_KEY: list(BUTTON_MAP.values()),
    e.EV_ABS: [
        (e.ABS_X,     AXIS_SETUP),
        (e.ABS_Y,     AXIS_SETUP),
        (e.ABS_RX,    AXIS_SETUP),
        (e.ABS_RY,    AXIS_SETUP),
        (e.ABS_Z,     TRIG_SETUP),
        (e.ABS_RZ,    TRIG_SETUP),
        (e.ABS_HAT0X, HAT_SETUP),
        (e.ABS_HAT0Y, HAT_SETUP),
    ],
}

# ── Virtual gamepads (one per controller index, created on first packet) ─────
# Use the exact VID/PID so SDL2 matches it against its gamepad database
pads = {}   # index -> {'ui': UInput, 'prev_btns': int}

def get_pad(idx):
    pad = pads.get(idx)
    if pad is not None:
        return pad
    if len(pads) >= MAX_PADS:
        return None
    ui = UInput(
        CAPABILITIES,
        name='Microsoft X-Box 360 pad',
        vendor=0x24c6,
        product=0x531a,
        version=0x0110,
    )
    try:
        path = ui.device.path if ui.device else "unknown"
    except Exception:
        path = "unknown"
    sys.stderr.write(f"Virtual gamepad {idx} active: {path}\n")
    sys.stderr.flush()
    pad = {'ui': ui, 'prev_btns': 0}
    pads[idx] = pad
    return pad

# ── Main loop ─────────────────────────────────────────────────────────────────
stdin = sys.stdin.buffer

def clamp16(v):
    """Prevent signed-short overflow when negating -32768."""
    return max(-32767, min(32767, v))

try:
    while True:
        data = stdin.read(PACKET_SIZE)
        if len(data) < PACKET_SIZE:
            break

        idx, buttons, lx, ly, rx, ry, lt, rt = PACKET_FMT.unpack(data)
        pad = get_pad(idx)
        if pad is None:
            continue
        ui = pad['ui']

        # ── Face / shoulder / stick-click buttons ─────────────────────────────
        changed = buttons ^ pad['prev_btns']
        for mask, code in BUTTON_MAP.items():
            if changed & mask:
                ui.write(e.EV_KEY, code, 1 if (buttons & mask) else 0)
        pad['prev_btns'] = buttons

        # ── D-pad as HAT axes ─────────────────────────────────────────────────
        hat_x = (1 if buttons & 0x0008 else 0) - (1 if buttons & 0x0004 else 0)
        hat_y = (1 if buttons & 0x0002 else 0) - (1 if buttons & 0x0001 else 0)
        ui.write(e.EV_ABS, e.ABS_HAT0X, hat_x)
        ui.write(e.EV_ABS, e.ABS_HAT0Y, hat_y)

        # ── Analog sticks (invert Y: XInput up=+, Linux up=-) ─────────────────
        ui.write(e.EV_ABS, e.ABS_X,  clamp16(lx))
        ui.write(e.EV_ABS, e.ABS_Y,  clamp16(-ly))
        ui.write(e.EV_ABS, e.ABS_RX, clamp16(rx))
        ui.write(e.EV_ABS, e.ABS_RY, clamp16(-ry))

        # ── Triggers ──────────────────────────────────────────────────────────
        ui.write(e.EV_ABS, e.ABS_Z,  lt)
        ui.write(e.EV_ABS, e.ABS_RZ, rt)

        # ── Sync event ────────────────────────────────────────────────────────
        ui.syn()

except KeyboardInterrupt:
    pass
finally:
    for pad in pads.values():
        pad['ui'].close()
    sys.stderr.write("Bridge closed.\n")
