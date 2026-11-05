#!/usr/bin/env python3
"""OmniCon macro engine.

Executes macros created by the OmniCon Macro Studio (F2 in the game shell).
Macros are JSON files (see Macro.hpp/Macro.cpp) and come in two flavours:

  * timeline — a list of actions played back in order (optionally looping)
  * trigger  — a condition that fires a list of actions:
        - pixel:  pixel at (x, y) matches a colour within a tolerance
        - button: a keyboard key or mouse button is pressed

Input is injected at the OS level with PyAutoGUI, so macros work with any
console — built-in emulator backends and external launchers alike.

Usage:
  macro_engine.py run <macro.json> [--once]
  macro_engine.py run-all <dir> [--system NAME] [--game NAME]
  macro_engine.py pick

'pick' is the colour-matcher tool: it draws NO overlay, so the colours on
screen are never affected.  Hover the mouse over the pixel you want and click
(or press Enter in the terminal as a fallback).  The result is printed to
stdout as "x,y,r,g,b" for the shell to read back.

Requires: pyautogui (pip install pyautogui).  Button triggers and click-to-pick
additionally use pynput (pip install pynput); without it, pixel triggers and
timelines still work and 'pick' falls back to Enter-to-capture.
"""

import argparse
import json
import os
import signal
import sys
import threading
import time

try:
    import pyautogui
    pyautogui.FAILSAFE = False  # macros may legitimately park the cursor in a corner
except ImportError:
    sys.stderr.write(
        "macro_engine: pyautogui is not installed.\n"
        "Install it with:  pip install pyautogui\n")
    sys.exit(2)

try:
    from pynput import keyboard as _pynput_keyboard
    from pynput import mouse as _pynput_mouse
    HAVE_PYNPUT = True
except Exception:  # ImportError, or no X display for pynput to hook
    HAVE_PYNPUT = False

_STOP = threading.Event()


def _handle_signal(signum, _frame):
    _STOP.set()


signal.signal(signal.SIGTERM, _handle_signal)
signal.signal(signal.SIGINT, _handle_signal)


# ─── Actions ──────────────────────────────────────────────────────────────────

_MOUSE_BUTTONS = {"left", "right", "middle"}


def _warn_unknown_key(key):
    if key and key not in pyautogui.KEYBOARD_KEYS:
        sys.stderr.write("macro_engine: warning: unknown key name %r\n" % key)


def run_action(action):
    kind = action.get("type", "key")
    key = action.get("key", "")
    x = int(action.get("x", -1))
    y = int(action.get("y", -1))
    ms = int(action.get("ms", 0))

    if kind == "key":
        _warn_unknown_key(key)
        pyautogui.press(key)
    elif kind == "keydown":
        _warn_unknown_key(key)
        pyautogui.keyDown(key)
    elif kind == "keyup":
        _warn_unknown_key(key)
        pyautogui.keyUp(key)
    elif kind == "click":
        button = key if key in _MOUSE_BUTTONS else "left"
        if x >= 0 and y >= 0:
            pyautogui.click(x, y, button=button)
        else:
            pyautogui.click(button=button)
    elif kind == "move":
        pyautogui.moveTo(x, y)
    elif kind == "wait":
        time.sleep(max(0, ms) / 1000.0)
    elif kind == "type":
        text = action.get("text", "")
        interval = max(0, ms) / 1000.0 if ms else 0.0
        pyautogui.typewrite(text, interval=interval) if text else None
    else:
        sys.stderr.write("macro_engine: warning: unknown action type %r\n" % kind)


def run_actions(actions):
    for action in actions:
        if _STOP.is_set():
            return
        run_action(action)


# ─── Timeline macros ──────────────────────────────────────────────────────────

def run_timeline(macro):
    actions = macro.get("actions", [])
    loop = bool(macro.get("loop", False))
    loop_delay = int(macro.get("loopDelayMs", 0)) / 1000.0
    while not _STOP.is_set():
        run_actions(actions)
        if not loop:
            break
        if loop_delay > 0:
            _STOP.wait(loop_delay)


# ─── Trigger macros ───────────────────────────────────────────────────────────

def _pixel_matches(x, y, rgb, tolerance):
    try:
        return pyautogui.pixelMatchesColor(x, y, tuple(rgb), tolerance=tolerance)
    except OSError as exc:
        sys.stderr.write("macro_engine: pixel read failed: %s\n" % exc)
        return False


def _run_pixel_trigger(macro, trigger):
    x = int(trigger.get("x", 0))
    y = int(trigger.get("y", 0))
    rgb = (int(trigger.get("r", 255)), int(trigger.get("g", 255)),
           int(trigger.get("b", 255)))
    tolerance = int(trigger.get("tolerance", 10))
    interval = max(10, int(trigger.get("intervalMs", 250))) / 1000.0
    cooldown = max(0, int(trigger.get("cooldownMs", 1000))) / 1000.0
    once = bool(trigger.get("once", False))
    last_fire = 0.0
    while not _STOP.is_set():
        now = time.monotonic()
        if now - last_fire >= cooldown and _pixel_matches(x, y, rgb, tolerance):
            last_fire = now
            run_actions(macro.get("actions", []))
            if once:
                return
        _STOP.wait(interval)


def _pynput_button_name(trigger):
    """Normalise the configured button to (device, name)."""
    name = str(trigger.get("button", "")).strip().lower()
    if name in _MOUSE_BUTTONS:
        return "mouse", name
    return "keyboard", name


def _run_button_trigger(macro, trigger):
    if not HAVE_PYNPUT:
        sys.stderr.write(
            "macro_engine: button trigger for %r needs pynput "
            "(pip install pynput); macro %r skipped.\n"
            % (trigger.get("button", ""), macro.get("name", "")))
        return

    device, wanted = _pynput_button_name(trigger)
    cooldown = max(0, int(trigger.get("cooldownMs", 1000))) / 1000.0
    once = bool(trigger.get("once", False))
    fired = threading.Event()
    state = {"last": 0.0}

    def maybe_fire():
        now = time.monotonic()
        if now - state["last"] < cooldown:
            return
        state["last"] = now
        fired.set()

    def on_key_press(key):
        try:
            name = key.char if key.char is not None else key.name
        except AttributeError:
            name = getattr(key, "name", str(key))
        if name and name.lower() == wanted:
            maybe_fire()

    def on_click(_x, _y, button, pressed):
        if pressed and button.name == wanted:
            maybe_fire()

    listeners = []
    if device == "keyboard":
        listeners.append(_pynput_keyboard.Listener(on_press=on_key_press))
    else:
        listeners.append(_pynput_mouse.Listener(on_click=on_click))
    for listener in listeners:
        listener.daemon = True
        listener.start()
    try:
        while not _STOP.is_set():
            if fired.wait(0.1):
                fired.clear()
                run_actions(macro.get("actions", []))
                if once:
                    return
    finally:
        for listener in listeners:
            listener.stop()


def run_trigger(macro):
    trigger = macro.get("trigger", {})
    kind = trigger.get("type", "none")
    if kind == "pixel":
        _run_pixel_trigger(macro, trigger)
    elif kind == "button":
        _run_button_trigger(macro, trigger)
    else:
        sys.stderr.write("macro_engine: macro %r has no trigger configured.\n"
                         % macro.get("name", ""))


def run_macro(macro):
    if macro.get("mode", "timeline") == "trigger":
        run_trigger(macro)
    else:
        run_timeline(macro)

#test
# ─── Store loading / filtering (mirrors Macro.cpp) ───────────────────────────

def load_macro(path):
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle)


def macro_matches(macro, system_name, game_name):
    if not macro.get("enabled", True):
        return False
    if not macro.get("actions"):
        return False
    if macro.get("mode") == "trigger" and \
            macro.get("trigger", {}).get("type", "none") == "none":
        return False
    system = macro.get("system", "any")
    if system and system != "any" and system != system_name:
        return False
    game = macro.get("game", "")
    if game and game != game_name:
        return False
    return True


def write_pid_file(directory):
    try:
        os.makedirs(directory, exist_ok=True)
        with open(os.path.join(directory, "engine.pid"), "w",
                  encoding="utf-8") as handle:
            handle.write(str(os.getpid()))
    except OSError:
        pass


def clear_pid_file(directory):
    try:
        os.remove(os.path.join(directory, "engine.pid"))
    except OSError:
        pass


# ─── Colour matcher ("pick") ─────────────────────────────────────────────────
# No overlay is ever drawn, so the colours on screen are exactly what the game
# renders — the captured value is safe to use in a pixel trigger.

def pick():
    sys.stderr.write(
        "macro_engine pick: hover over the pixel you want, then click "
        "(or press Enter here).\n")

    result = {}

    def capture():
        x, y = pyautogui.position()
        r, g, b = pyautogui.pixel(x, y)
        result["value"] = (x, y, r, g, b)

    if HAVE_PYNPUT:
        done = threading.Event()

        def on_click(_x, _y, _button, pressed):
            if pressed:
                capture()
                done.set()
                return False  # stop listener

        listener = _pynput_mouse.Listener(on_click=on_click)
        listener.daemon = True
        listener.start()
        # Enter-to-capture fallback works even with pynput present.
        while not done.is_set():
            try:
                import select
                ready, _, _ = select.select([sys.stdin], [], [], 0.1)
                if ready:
                    sys.stdin.readline()
                    capture()
                    break
            except (OSError, ValueError):
                done.wait(0.1)
        listener.stop()
    else:
        sys.stderr.write("macro_engine pick: pynput not available; "
                         "hover the pixel and press Enter.\n")
        try:
            sys.stdin.readline()
        except KeyboardInterrupt:
            return 1
        capture()

    if "value" not in result:
        return 1
    x, y, r, g, b = result["value"]
    sys.stdout.write("%d,%d,%d,%d,%d\n" % (x, y, r, g, b))
    sys.stdout.flush()
    return 0


# ─── Entry point ──────────────────────────────────────────────────────────────

def main(argv=None):
    parser = argparse.ArgumentParser(description="OmniCon macro engine")
    sub = parser.add_subparsers(dest="command", required=True)

    p_run = sub.add_parser("run", help="run a single macro file")
    p_run.add_argument("macro_file")

    p_all = sub.add_parser("run-all", help="run every matching macro in a directory")
    p_all.add_argument("directory")
    p_all.add_argument("--system", default="")
    p_all.add_argument("--game", default="")

    sub.add_parser("pick", help="capture a pixel position and colour")

    args = parser.parse_args(argv)

    if args.command == "pick":
        return pick()

    if args.command == "run":
        run_macro(load_macro(args.macro_file))
        return 0

    # run-all
    macros = []
    for entry in sorted(os.listdir(args.directory)):
        if not entry.endswith(".json"):
            continue
        try:
            macro = load_macro(os.path.join(args.directory, entry))
        except (OSError, ValueError) as exc:
            sys.stderr.write("macro_engine: skipping %s: %s\n" % (entry, exc))
            continue
        if macro_matches(macro, args.system, args.game):
            macros.append(macro)

    if not macros:
        sys.stderr.write("macro_engine: no enabled macros match system=%r game=%r.\n"
                         % (args.system, args.game))
        return 0

    write_pid_file(args.directory)
    sys.stderr.write("macro_engine: running %d macro(s): %s\n"
                     % (len(macros), ", ".join(m.get("name", "?") for m in macros)))
    try:
        threads = [threading.Thread(target=run_macro, args=(m,), daemon=True)
                   for m in macros]
        for thread in threads:
            thread.start()
        while any(thread.is_alive() for thread in threads) and not _STOP.is_set():
            _STOP.wait(0.2)
    finally:
        clear_pid_file(args.directory)
    return 0


if __name__ == "__main__":
    sys.exit(main())
