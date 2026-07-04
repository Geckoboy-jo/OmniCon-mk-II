#!/usr/bin/env python3
"""
Windows-side controller reader.
Tries XInput first (Xbox controllers); falls back to WinMM for non-XInput
devices such as the BDA NSW Fusion Pro (Switch Pro layout) and generic HID
gamepads.

Each packet written to stdout is 12 bytes:
  <H h h h h B B>
  buttons (uint16), lx, ly, rx, ry (int16), lt, rt (uint8)

XInput button bitmask:
  0x0001 DPAD_UP    0x0002 DPAD_DOWN  0x0004 DPAD_LEFT  0x0008 DPAD_RIGHT
  0x0010 START      0x0020 BACK       0x0040 LTHUMB      0x0080 RTHUMB
  0x0100 LSHOULDER  0x0200 RSHOULDER
  0x1000 A (south)  0x2000 B (east)   0x4000 X (west)   0x8000 Y (north)
"""
import ctypes
import struct
import sys
import os
import time

PACKET_FMT    = struct.Struct('<HhhhhBB')
POLL_INTERVAL = 1.0 / 250.0
ERROR_DEVICE_NOT_READY = 1167

# ─── XInput ──────────────────────────────────────────────────────────────────

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

def _load_xinput():
    for lib in ('xinput1_4', 'xinput1_3', 'xinput9_1_0'):
        try:
            x = ctypes.windll.LoadLibrary(lib)
            sys.stderr.write(f'[xinput] Loaded {lib}\n'); sys.stderr.flush()
            return x
        except OSError:
            pass
    return None

# ─── WinMM ───────────────────────────────────────────────────────────────────

MAXPNAMELEN            = 32
MAX_JOYSTICKOEMVXDNAME = 260

class JOYCAPS(ctypes.Structure):
    _fields_ = [
        ('wMid',        ctypes.c_ushort),
        ('wPid',        ctypes.c_ushort),
        ('szPname',     ctypes.c_char * MAXPNAMELEN),
        ('wXmin',       ctypes.c_uint),
        ('wXmax',       ctypes.c_uint),
        ('wYmin',       ctypes.c_uint),
        ('wYmax',       ctypes.c_uint),
        ('wZmin',       ctypes.c_uint),
        ('wZmax',       ctypes.c_uint),
        ('wNumButtons', ctypes.c_uint),
        ('wPeriodMin',  ctypes.c_uint),
        ('wPeriodMax',  ctypes.c_uint),
        ('wRmin',       ctypes.c_uint),
        ('wRmax',       ctypes.c_uint),
        ('wUmin',       ctypes.c_uint),
        ('wUmax',       ctypes.c_uint),
        ('wVmin',       ctypes.c_uint),
        ('wVmax',       ctypes.c_uint),
        ('wCaps',       ctypes.c_uint),
        ('wMaxAxes',    ctypes.c_uint),
        ('wNumAxes',    ctypes.c_uint),
        ('wMaxButtons', ctypes.c_uint),
        ('szRegKey',    ctypes.c_char * MAXPNAMELEN),
        ('szOEMVxD',    ctypes.c_char * MAX_JOYSTICKOEMVXDNAME),
    ]

class JOYINFOEX(ctypes.Structure):
    _fields_ = [
        ('dwSize',         ctypes.c_uint32),
        ('dwFlags',        ctypes.c_uint32),
        ('dwXpos',         ctypes.c_uint32),
        ('dwYpos',         ctypes.c_uint32),
        ('dwZpos',         ctypes.c_uint32),
        ('dwRpos',         ctypes.c_uint32),
        ('dwUpos',         ctypes.c_uint32),
        ('dwVpos',         ctypes.c_uint32),
        ('dwButtons',      ctypes.c_uint32),
        ('dwButtonNumber', ctypes.c_uint32),
        ('dwPOV',          ctypes.c_uint32),
        ('dwReserved1',    ctypes.c_uint32),
        ('dwReserved2',    ctypes.c_uint32),
    ]

JOYERR_NOERROR = 0
JOY_RETURNALL  = 0xFF
POV_CENTERED   = 0xFFFFFFFF

def _axis(raw, lo, hi):
    """Map WinMM axis (lo..hi) → int16 (-32767..32767)."""
    if hi <= lo:
        return 0
    v = int((raw - lo) * 65534 / (hi - lo)) - 32767
    return max(-32767, min(32767, v))

def _pov_to_dpad(pov):
    """Convert WinMM POV (centidegrees) → XInput D-pad bitmask."""
    if pov == POV_CENTERED or pov > 36000:
        return 0
    deg = pov / 100.0
    bits = 0
    if   deg <= 22.5 or deg >= 337.5:  bits = 0x0001
    elif deg <  67.5:                  bits = 0x0001 | 0x0008
    elif deg <= 112.5:                 bits = 0x0008
    elif deg <  157.5:                 bits = 0x0002 | 0x0008
    elif deg <= 202.5:                 bits = 0x0002
    elif deg <  247.5:                 bits = 0x0002 | 0x0004
    elif deg <= 292.5:                 bits = 0x0004
    elif deg <  337.5:                 bits = 0x0001 | 0x0004
    return bits

# ── Button mapping: WinMM bit index → XInput button bitmask ──────────────────
# Default matches Nintendo Switch Pro layout / BDA NSW Fusion Pro.
# Positional (south/east/north/west) mapping so SDL abstract buttons work
# correctly regardless of physical label.
#
# Override per-bit with env var: CONTROLLER_BTN_MAP="0:0x1000,1:0x2000,..."
#
# Switch physical → XInput semantic:
#  bit 0  B (south)  → A  0x1000
#  bit 1  A (east)   → B  0x2000
#  bit 2  Y (west)   → X  0x4000
#  bit 0  Y (west)   → X    0x4000  (confirmed via diagnose_controller.py)
#  bit 1  B (south)  → A    0x1000
#  bit 2  A (east)   → B    0x2000
#  bit 3  X (north)  → Y    0x8000
#  bit 4  L          → LB   0x0100
#  bit 5  R          → RB   0x0200
#  bit 6  ZL         → sent as lt byte (255)
#  bit 7  ZR         → sent as rt byte (255)
#  bit 8  Minus      → Back  0x0020
#  bit 9  Plus       → Start 0x0010
#  bit 10 L3         → LThumb 0x0040
#  bit 11 R3         → RThumb 0x0080
DEFAULT_BTN_MAP = {
    0:  0x4000,  # Y (west)  → X
    1:  0x1000,  # B (south) → A
    2:  0x2000,  # A (east)  → B
    3:  0x8000,  # X (north) → Y
    4:  0x0100,  # L         → LB
    5:  0x0200,  # R         → RB
    8:  0x0020,  # Minus     → Back
    9:  0x0010,  # Plus      → Start
    10: 0x0040,  # L3        → LThumb
    11: 0x0080,  # R3        → RThumb
}
ZL_BIT = 6   # bit that acts as left trigger
ZR_BIT = 7   # bit that acts as right trigger

def _load_btn_map():
    raw = os.environ.get('CONTROLLER_BTN_MAP', '')
    if not raw:
        return DEFAULT_BTN_MAP
    m = {}
    for pair in raw.split(','):
        pair = pair.strip()
        if ':' in pair:
            k, v = pair.split(':', 1)
            try:
                m[int(k)] = int(v, 0)
            except ValueError:
                pass
    return m or DEFAULT_BTN_MAP

# ── Axis assignment ───────────────────────────────────────────────────────────
# Override with: CONTROLLER_AXIS_MAP="lx:X,ly:Y,rx:Z,ry:R,lt:none,rt:none"
# Axis names: X Y Z R U V  (WinMM axis identifiers)
# 'none' for lt/rt means use the ZL/ZR button bits instead.
DEFAULT_AXIS_MAP = {'lx': 'X', 'ly': 'Y', 'rx': 'Z', 'ry': 'R',
                    'lt': 'none', 'rt': 'none'}

def _load_axis_map():
    raw = os.environ.get('CONTROLLER_AXIS_MAP', '')
    if not raw:
        return DEFAULT_AXIS_MAP
    m = dict(DEFAULT_AXIS_MAP)
    for pair in raw.split(','):
        pair = pair.strip()
        if ':' in pair:
            k, v = pair.split(':', 1)
            k = k.strip().lower(); v = v.strip().upper()
            if k in m:
                m[k] = v
    return m

def _read_axis(info, caps, name):
    n = name.upper()
    if n == 'X':  return _axis(info.dwXpos, caps.wXmin, caps.wXmax)
    if n == 'Y':  return -_axis(info.dwYpos, caps.wYmin, caps.wYmax)  # invert
    if n == 'Z':  return _axis(info.dwZpos, caps.wZmin, caps.wZmax)
    if n == 'R':  return -_axis(info.dwRpos, caps.wRmin, caps.wRmax)  # invert
    if n == 'U':  return _axis(info.dwUpos, caps.wUmin, caps.wUmax)
    if n == 'V':  return _axis(info.dwVpos, caps.wVmin, caps.wVmax)
    return 0

def _read_trigger(info, caps, name, raw_btns, is_lt):
    n = name.upper()
    if n in ('NONE', ''):
        bit = ZL_BIT if is_lt else ZR_BIT
        return 255 if (raw_btns >> bit) & 1 else 0
    v = _read_axis(info, caps, n)
    return min(255, max(0, (v + 32767) >> 8))

# ─── XInput runner ────────────────────────────────────────────────────────────

def run_xinput(xi):
    state      = XINPUT_STATE()
    prev_pkt   = -1
    sys.stderr.write('[xinput] Running (Xbox-compatible mode).\n'); sys.stderr.flush()
    while True:
        ret = xi.XInputGetState(0, ctypes.byref(state))
        if ret == 0:
            if state.dwPacketNumber != prev_pkt:
                prev_pkt = state.dwPacketNumber
                g = state.Gamepad
                sys.stdout.buffer.write(PACKET_FMT.pack(
                    g.wButtons, g.sThumbLX, g.sThumbLY,
                    g.sThumbRX, g.sThumbRY,
                    g.bLeftTrigger, g.bRightTrigger))
                sys.stdout.buffer.flush()
        elif ret == ERROR_DEVICE_NOT_READY:
            time.sleep(1.0)
            continue
        time.sleep(POLL_INTERVAL)

# ─── WinMM runner ─────────────────────────────────────────────────────────────

def run_winmm():
    """Returns True on clean exit, False if controller not found / disconnected."""
    winmm = ctypes.windll.winmm
    caps  = JOYCAPS()
    joy_id = -1
    for jid in range(winmm.joyGetNumDevs()):
        if winmm.joyGetDevCapsA(jid, ctypes.byref(caps), ctypes.sizeof(caps)) == JOYERR_NOERROR:
            joy_id = jid
            break
    if joy_id < 0:
        sys.stderr.write('[winmm] No joystick found — waiting...\n'); sys.stderr.flush()
        return False

    name = caps.szPname.decode('ascii', errors='replace')
    sys.stderr.write(f'[winmm] Joystick {joy_id}: "{name}"  '
                     f'buttons={caps.wNumButtons} axes={caps.wNumAxes}\n')
    sys.stderr.flush()

    btn_map  = _load_btn_map()
    axis_map = _load_axis_map()
    info     = JOYINFOEX()
    info.dwSize  = ctypes.sizeof(JOYINFOEX)
    info.dwFlags = JOY_RETURNALL
    prev_data    = None

    while True:
        ret = winmm.joyGetPosEx(joy_id, ctypes.byref(info))
        if ret != JOYERR_NOERROR:
            time.sleep(1.0)
            if winmm.joyGetDevCapsA(joy_id, ctypes.byref(caps), ctypes.sizeof(caps)) != JOYERR_NOERROR:
                sys.stderr.write('[winmm] Controller disconnected.\n'); sys.stderr.flush()
                return False
            continue

        raw_btns   = info.dwButtons
        xi_buttons = _pov_to_dpad(info.dwPOV)
        for bit, mask in btn_map.items():
            if (raw_btns >> bit) & 1:
                xi_buttons |= mask

        lx = _read_axis(info, caps, axis_map['lx'])
        ly = _read_axis(info, caps, axis_map['ly'])
        rx = _read_axis(info, caps, axis_map['rx'])
        ry = _read_axis(info, caps, axis_map['ry'])
        lt = _read_trigger(info, caps, axis_map['lt'], raw_btns, True)
        rt = _read_trigger(info, caps, axis_map['rt'], raw_btns, False)

        cur = (xi_buttons, lx, ly, rx, ry, lt, rt)
        if cur != prev_data:
            prev_data = cur
            sys.stdout.buffer.write(PACKET_FMT.pack(*cur))
            sys.stdout.buffer.flush()

        time.sleep(POLL_INTERVAL)

# ─── Entry point ──────────────────────────────────────────────────────────────

def main():
    sys.stderr.write('[bridge] Starting controller bridge...\n'); sys.stderr.flush()

    xi = _load_xinput()
    if xi is not None:
        state = XINPUT_STATE()
        if xi.XInputGetState(0, ctypes.byref(state)) == 0:
            run_xinput(xi)   # Xbox controller present — use XInput
            return
        sys.stderr.write('[xinput] No XInput controller — switching to WinMM.\n')
    else:
        sys.stderr.write('[xinput] XInput DLL unavailable — switching to WinMM.\n')
    sys.stderr.write('[winmm] WinMM mode active (Switch Pro / HID gamepad support).\n')
    sys.stderr.flush()

    while True:
        if not run_winmm():
            time.sleep(2.0)   # retry after disconnect / not found

if __name__ == '__main__':
    main()
