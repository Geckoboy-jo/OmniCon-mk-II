#!/bin/bash
# controller_bridge/start_bridge.sh
# Starts the XInput -> uinput controller bridge.
# Usage: ./controller_bridge/start_bridge.sh

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENV_PYTHON="$SCRIPT_DIR/../.venv/bin/python3"

# Load uinput kernel module
echo "Loading uinput module..."
sudo modprobe uinput
sudo chmod 666 /dev/uinput

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

echo "Windows Python : $WIN_PYTHON"
echo "WSL Python     : $VENV_PYTHON"

WIN_SERVER="$(wslpath -w "$SCRIPT_DIR/xinput_server.py")"
echo "XInput server  : $WIN_SERVER"
echo ""
echo "Bridge starting -- press Ctrl+C to stop."
echo ""

# Auto-chmod the new event device after the bridge creates it
(sleep 3 && sudo chmod 666 /dev/input/event* 2>/dev/null && echo "Input device permissions set.") &

# Pipe: Windows XInput reader -> WSL uinput writer
"$WIN_PYTHON" "$WIN_SERVER" 2>&1 | "$VENV_PYTHON" "$SCRIPT_DIR/uinput_client.py"
