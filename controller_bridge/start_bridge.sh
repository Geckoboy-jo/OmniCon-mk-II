#!/bin/bash
# controller_bridge/start_bridge.sh
# Starts the XInput -> uinput controller bridge.
# Usage: ./controller_bridge/start_bridge.sh

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENV_PYTHON="$SCRIPT_DIR/../.venv/bin/python3"

# Guard: exit silently if the bridge is already running
if pgrep -f "[u]input_client.py" > /dev/null 2>&1; then
    echo "Controller bridge already running."
    exit 0
fi

# Fall back to system python3 if the venv doesn't exist
if [ ! -x "$VENV_PYTHON" ]; then
    VENV_PYTHON="$(command -v python3)"
    if [ -z "$VENV_PYTHON" ]; then
        echo "ERROR: No Python 3 found. Install python3 or create a .venv."
        exit 1
    fi
fi

LOGFILE="/tmp/omnicon_bridge.log"
fail() { echo "ERROR: $*" | tee -a "$LOGFILE" >&2; exit 1; }

# Load uinput kernel module (no-op if already loaded; works as root)
modprobe uinput 2>/dev/null || sudo -n modprobe uinput 2>/dev/null || true

"$VENV_PYTHON" -c "import evdev" 2>/dev/null \
    || fail "python evdev is missing for $VENV_PYTHON. Run scripts/setup_wsl_controller.sh"
[ -w /dev/uinput ] \
    || fail "/dev/uinput is missing or not writable. Run scripts/setup_wsl_controller.sh, then 'wsl --shutdown' from Windows."

# Locate Windows Python
WIN_PYTHON=""
for candidate in python.exe py.exe; do
    if command -v "$candidate" > /dev/null 2>&1; then
        WIN_PYTHON="$candidate"
        break
    fi
done

if [ -z "$WIN_PYTHON" ]; then
    echo "ERROR: Windows Python not found. Install Python 3 for Windows."
    exit 1
fi

WIN_SERVER="$(wslpath -w "$SCRIPT_DIR/xinput_server.py")"
echo "Controller bridge starting (XInput → uinput)..."  | tee -a "$LOGFILE"
echo "[bridge] xinput_server : $WIN_SERVER"              >> "$LOGFILE"
echo "[bridge] uinput_client : $VENV_PYTHON $SCRIPT_DIR/uinput_client.py" >> "$LOGFILE"

# Pipe ONLY stdout (binary packets) from the Windows reader to the WSL writer.
# Stderr is redirected to the log file so it never pollutes the binary stream.
"$WIN_PYTHON" "$WIN_SERVER" 2>>"$LOGFILE" | "$VENV_PYTHON" "$SCRIPT_DIR/uinput_client.py" 2>>"$LOGFILE"
