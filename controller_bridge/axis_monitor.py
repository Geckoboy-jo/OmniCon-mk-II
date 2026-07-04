#!/usr/bin/env python3
"""Real-time axis monitor. Run: py.exe axis_monitor.py
Move each stick and watch which value changes."""
import ctypes, time, sys, os

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

winmm  = ctypes.windll.winmm
info   = JOYINFOEX()
info.dwSize  = ctypes.sizeof(JOYINFOEX)
info.dwFlags = 0xFF  # JOY_RETURNALL
joy_id = 0

def bar(v, lo=0, hi=65535, width=20):
    pct = (v - lo) / max(hi - lo, 1)
    n   = int(pct * width)
    mid = width // 2
    if abs(n - mid) < 1:
        return '|' + ' ' * (mid - 1) + 'o' + ' ' * (mid - 1) + '|'
    mark = '<' if n < mid else '>'
    b = [' '] * width
    b[mid] = '|'
    b[max(0, min(width - 1, n))] = mark
    return '|' + ''.join(b) + '|'

print("Move sticks — watch which axis changes.  Ctrl+C to stop.\n")
print("  Axis X = Left-Stick-H?   Axis Y = Left-Stick-V?")
print("  Axis Z = Right-Stick-H?  Axis R = Right-Stick-V?\n")

prev_line = ""
try:
    while True:
        winmm.joyGetPosEx(joy_id, ctypes.byref(info))
        line = (
            f"\r  X={info.dwXpos:5d}{bar(info.dwXpos)}"
            f"  Y={info.dwYpos:5d}{bar(info.dwYpos)}"
            f"  Z={info.dwZpos:5d}{bar(info.dwZpos)}"
            f"  R={info.dwRpos:5d}{bar(info.dwRpos)}"
            f"  POV={info.dwPOV:6d}"
            f"  BTN=0x{info.dwButtons:04x}"
        )
        if line != prev_line:
            sys.stdout.write(line)
            sys.stdout.flush()
            prev_line = line
        time.sleep(1/30)
except KeyboardInterrupt:
    print("\nDone.")
