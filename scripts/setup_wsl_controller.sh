#!/usr/bin/env bash
# One-time WSL2 setup for the controller bridge (Windows XInput -> uinput).
#
#   scripts/setup_wsl_controller.sh
#
# Makes the bridge work for a normal (non-root) user on Ubuntu 24.04:
#   * installs python3-evdev (pip is blocked on 24.04 by PEP 668)
#   * loads the uinput module at every boot and makes /dev/uinput usable by the
#     "input" group
#   * enables systemd in /etc/wsl.conf so udev gives the virtual gamepad's
#     /dev/input/event* node the same group access (SDL / SameBoy need to open it)
#   * adds the user to the "input" group
# Idempotent. Run `wsl --shutdown` from Windows afterwards to apply the changes.

set -u

grep -qi microsoft /proc/version 2>/dev/null || { echo "Not running under WSL; nothing to do."; exit 0; }

if [ "$(id -u)" -ne 0 ]; then
    exec sudo bash "$0" "${SUDO_USER:-$USER}"
fi

TARGET_USER="${1:-${SUDO_USER:-}}"
log() { printf '[wsl-controller] %s\n' "$*"; }

if ! python3 -c 'import evdev' 2>/dev/null; then
    log "Installing python3-evdev..."
    apt-get update -qq && apt-get install -y python3-evdev || log "WARNING: could not install python3-evdev"
fi

# A boot unit is used instead of a udev rule: udev may handle /dev/uinput before
# or after the module loads, which left it root-only (0600) some boots.
rm -f /etc/modules-load.d/uinput.conf /etc/udev/rules.d/99-omnicon-uinput.rules
cat > /etc/systemd/system/omnicon-uinput.service <<'EOF'
[Unit]
Description=Load uinput and let the input group use it (OmniCon controller bridge)

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/bin/sh -c 'modprobe uinput && chgrp input /dev/uinput && chmod 660 /dev/uinput'

[Install]
WantedBy=multi-user.target
EOF
systemctl daemon-reload 2>/dev/null
systemctl enable omnicon-uinput.service >/dev/null 2>&1

if [ -n "$TARGET_USER" ] && id "$TARGET_USER" >/dev/null 2>&1; then
    usermod -aG input "$TARGET_USER"
fi

# Enable systemd, keeping any other settings already in /etc/wsl.conf.
touch /etc/wsl.conf
if grep -q '^\s*systemd\s*=' /etc/wsl.conf; then
    sed -i 's/^\s*systemd\s*=.*/systemd=true/' /etc/wsl.conf
elif grep -q '^\[boot\]' /etc/wsl.conf; then
    sed -i '/^\[boot\]/a systemd=true' /etc/wsl.conf
else
    printf '\n[boot]\nsystemd=true\n' >> /etc/wsl.conf
fi

# Take effect right now too, without waiting for the next WSL restart.
modprobe uinput 2>/dev/null
if [ -e /dev/uinput ]; then
    chgrp input /dev/uinput
    chmod 660 /dev/uinput
fi

log "Done. Run 'wsl --shutdown' from Windows, then start OmniCon again."
