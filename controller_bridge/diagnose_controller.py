#!/usr/bin/env python3
"""
Interactive controller mapping diagnostic.
Run: py.exe diagnose_controller.py
Follow the prompts — press each button when asked.
At the end it prints a complete mapping you can paste into xinput_server.py.
"""
import ctypes, time, sys

MAXPNAMELEN            = 32
MAX_JOYSTICKOEMVXDNAME = 260

class JOYCAPS(ctypes.Structure):
    _fields_ = [
        ('wMid',        ctypes.c_ushort), ('wPid',  ctypes.c_ushort),
        ('szPname',     ctypes.c_char * MAXPNAMELEN),
        ('wXmin', ctypes.c_uint), ('wXmax', ctypes.c_uint),
        ('wYmin', ctypes.c_uint), ('wYmax', ctypes.c_uint),
        ('wZmin', ctypes.c_uint), ('wZmax', ctypes.c_uint),
        ('wNumButtons', ctypes.c_uint),
        ('wPeriodMin', ctypes.c_uint), ('wPeriodMax', ctypes.c_uint),
        ('wRmin', ctypes.c_uint), ('wRmax', ctypes.c_uint),
        ('wUmin', ctypes.c_uint), ('wUmax', ctypes.c_uint),
        ('wVmin', ctypes.c_uint), ('wVmax', ctypes.c_uint),
        ('wCaps', ctypes.c_uint),
        ('wMaxAxes', ctypes.c_uint), ('wNumAxes', ctypes.c_uint),
        ('wMaxButtons', ctypes.c_uint),
        ('szRegKey', ctypes.c_char * MAXPNAMELEN),
        ('szOEMVxD', ctypes.c_char * MAX_JOYSTICKOEMVXDNAME),
    ]

class JOYINFOEX(ctypes.Structure):
    _fields_ = [
        ('dwSize', ctypes.c_uint32), ('dwFlags', ctypes.c_uint32),
        ('dwXpos', ctypes.c_uint32), ('dwYpos',  ctypes.c_uint32),
        ('dwZpos', ctypes.c_uint32), ('dwRpos',  ctypes.c_uint32),
        ('dwUpos', ctypes.c_uint32), ('dwVpos',  ctypes.c_uint32),
        ('dwButtons', ctypes.c_uint32), ('dwButtonNumber', ctypes.c_uint32),
        ('dwPOV', ctypes.c_uint32),
        ('dwReserved1', ctypes.c_uint32), ('dwReserved2', ctypes.c_uint32),
    ]

POV_CENTERED = 0xFFFFFFFF
JOYERR_NOERROR = 0
JOY_RETURNALL  = 0xFF

winmm  = ctypes.windll.winmm
caps   = JOYCAPS()
joy_id = -1
for jid in range(winmm.joyGetNumDevs()):
    if winmm.joyGetDevCapsA(jid, ctypes.byref(caps), ctypes.sizeof(caps)) == JOYERR_NOERROR:
        joy_id = jid; break

if joy_id < 0:
    print("No joystick found."); sys.exit(1)

name = caps.szPname.decode('ascii', errors='replace')
print(f"\nController detected: {name!r}  (id={joy_id})")
print(f"  {caps.wNumButtons} buttons, {caps.wNumAxes} axes, caps=0x{caps.wCaps:04x}\n")

info = JOYINFOEX()
info.dwSize  = ctypes.sizeof(JOYINFOEX)
info.dwFlags = JOY_RETURNALL

def read():
    winmm.joyGetPosEx(joy_id, ctypes.byref(info))
    return info

def wait_idle():
    """Wait until all buttons are released and sticks centred."""
    for _ in range(30):
        r = read()
        if r.dwButtons == 0:
            time.sleep(0.05)
    time.sleep(0.1)

def wait_button(label, timeout=8.0):
    """Wait for exactly one button to be pressed and return its bit index."""
    wait_idle()
    print(f"  >>> Press {label} now ... ", end='', flush=True)
    deadline = time.time() + timeout
    while time.time() < deadline:
        r = read()
        if r.dwButtons:
            # Find the first set bit
            bit = (r.dwButtons & -r.dwButtons).bit_length() - 1
            print(f"bit {bit}  (dwButtons=0x{r.dwButtons:08x})")
            wait_idle()
            return bit
        time.sleep(1/60)
    print("TIMEOUT")
    return None

def wait_axis(label, timeout=8.0):
    """Wait for a large axis deflection and return axis name + direction."""
    wait_idle()
    baseline = read()
    MID = {
        'X': (caps.wXmin + caps.wXmax) // 2,
        'Y': (caps.wYmin + caps.wYmax) // 2,
        'Z': (caps.wZmin + caps.wZmax) // 2,
        'R': (caps.wRmin + caps.wRmax) // 2,
        'U': (caps.wUmin + caps.wUmax) // 2,
        'V': (caps.wVmin + caps.wVmax) // 2,
    }
    RANGE = {
        'X': caps.wXmax - caps.wXmin, 'Y': caps.wYmax - caps.wYmin,
        'Z': caps.wZmax - caps.wZmin, 'R': caps.wRmax - caps.wRmin,
        'U': caps.wUmax - caps.wUmin, 'V': caps.wVmax - caps.wVmin,
    }
    print(f"  >>> Push {label} now ... ", end='', flush=True)
    deadline = time.time() + timeout
    while time.time() < deadline:
        r = read()
        vals = {'X': r.dwXpos, 'Y': r.dwYpos, 'Z': r.dwZpos,
                'R': r.dwRpos, 'U': r.dwUpos, 'V': r.dwVpos}
        best_ax, best_dev = None, 0
        for ax, v in vals.items():
            rng = RANGE[ax]
            if rng == 0: continue
            dev = abs(v - MID[ax]) / rng
            if dev > best_dev:
                best_dev, best_ax = dev, ax
        if best_dev > 0.4:
            v = vals[best_ax]
            direction = "+" if v > MID[best_ax] else "-"
            print(f"axis {best_ax}{direction}  (raw={v}, mid={MID[best_ax]})")
            wait_idle()
            return best_ax, direction
        time.sleep(1/60)
    print("TIMEOUT")
    return None, None

# ── Button mapping ─────────────────────────────────────────────────────────────
print("=" * 56)
print("BUTTON MAPPING — press each face/shoulder button when asked")
print("=" * 56)

BUTTONS = [
    ("B (south / bottom face)",  "south", 0x1000),
    ("A (east  / right  face)",  "east",  0x2000),
    ("X (north / top    face)",  "north", 0x8000),
    ("Y (west  / left   face)",  "west",  0x4000),
    ("L  (left  shoulder)",      "lb",    0x0100),
    ("R  (right shoulder)",      "rb",    0x0200),
    ("ZL (left  trigger)",       "zl",    None),   # treated as trigger byte
    ("ZR (right trigger)",       "zr",    None),
    ("−  (Minus / Select)",      "back",  0x0020),
    ("+  (Plus  / Start)",       "start", 0x0010),
    ("L3 (left  stick click)",   "ls",    0x0040),
    ("R3 (right stick click)",   "rs",    0x0080),
]

btn_results = {}
for label, key, xi_mask in BUTTONS:
    bit = wait_button(label)
    if bit is not None:
        btn_results[key] = (bit, xi_mask)

print()
print("=" * 56)
print("AXIS MAPPING — deflect each stick when asked")
print("=" * 56)

axis_results = {}
for label, key in [
    ("Left Stick RIGHT",  "lx+"),
    ("Left Stick DOWN",   "ly+"),
    ("Right Stick RIGHT", "rx+"),
    ("Right Stick DOWN",  "ry+"),
]:
    ax, direction = wait_axis(label)
    if ax:
        base_key = key[:2]  # 'lx', 'ly', 'rx', 'ry'
        axis_results[base_key] = ax

# ── Print results ──────────────────────────────────────────────────────────────
print()
print("=" * 56)
print("RESULTS — add to xinput_server.py DEFAULT_BTN_MAP / DEFAULT_AXIS_MAP")
print("=" * 56)

print("\nDEFAULT_BTN_MAP = {")
for key, (bit, xi_mask) in btn_results.items():
    if xi_mask is not None:
        print(f"    {bit}: 0x{xi_mask:04x},  # {key}")
print("}")

zl_bit = btn_results.get('zl', (None,))[0]
zr_bit = btn_results.get('zr', (None,))[0]
if zl_bit is not None: print(f"ZL_BIT = {zl_bit}")
if zr_bit is not None: print(f"ZR_BIT = {zr_bit}")

if axis_results:
    print("\nDEFAULT_AXIS_MAP = {")
    for k in ('lx','ly','rx','ry'):
        ax = axis_results.get(k, '?')
        print(f"    '{k}': '{ax}',")
    print("    'lt': 'none', 'rt': 'none',")
    print("}")

print("\nCopy the above into xinput_server.py and restart the bridge.")
