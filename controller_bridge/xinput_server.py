#!/usr/bin/env python3
"""
Windows-side XInput reader.
Polls controller at 250 Hz and streams binary state packets to stdout.
Each packet is 12 bytes: <H h h h h B B>
  buttons (uint16), lx, ly, rx, ry (int16 each), lt, rt (uint8 each)
"""
import ctypes
import struct
import sys
import time

# ── XInput structs ────────────────────────────────────────────────────────────

class XINPUT_GAMEPAD(ctypes.Structure):
    _fields_ = [
        ('wButtons',      ctypes.c_ushort),
        ('bLeftTrigger',  ctypes.c_ubyte),
        ('bRightTrigger', ctypes.c_ubyte),
        ('sThumbLX',      ctypes.c_short),
        ('sThumbLY',      ctypes.c_short),
        ('sThumbRX',      ctypes.c_short),
        ('sThumbRY',      ctypes.c_short),
    ]

class XINPUT_STATE(ctypes.Structure):
    _fields_ = [
        ('dwPacketNumber', ctypes.c_ulong),
        ('Gamepad',        XINPUT_GAMEPAD),
    ]

# ── Load XInput DLL ───────────────────────────────────────────────────────────

xinput = None
for lib in ('xinput1_4', 'xinput1_3', 'xinput9_1_0'):
    try:
        xinput = ctypes.windll.LoadLibrary(lib)
        sys.stderr.write(f"Loaded {lib}\n")
        sys.stderr.flush()
        break
    except OSError:
        pass

if xinput is None:
    sys.stderr.write("ERROR: No XInput DLL found\n")
    sys.exit(1)

# ── Main loop ─────────────────────────────────────────────────────────────────

PACKET_FMT   = struct.Struct('<HhhhhBB')   # 12 bytes
POLL_INTERVAL = 1.0 / 250.0               # 250 Hz
ERROR_DEVICE_NOT_READY = 1167

state = XINPUT_STATE()
sys.stderr.write("XInput server running — waiting for controller...\n")
sys.stderr.flush()

prev_packet = -1

while True:
    ret = xinput.XInputGetState(0, ctypes.byref(state))

    if ret == 0:                           # ERROR_SUCCESS
        if state.dwPacketNumber != prev_packet:
            prev_packet = state.dwPacketNumber
            g = state.Gamepad
            pkt = PACKET_FMT.pack(
                g.wButtons,
                g.sThumbLX, g.sThumbLY,
                g.sThumbRX, g.sThumbRY,
                g.bLeftTrigger, g.bRightTrigger,
            )
            sys.stdout.buffer.write(pkt)
            sys.stdout.buffer.flush()
    elif ret == ERROR_DEVICE_NOT_READY:
        time.sleep(1.0)                    # Controller unplugged; wait
        continue

    time.sleep(POLL_INTERVAL)
