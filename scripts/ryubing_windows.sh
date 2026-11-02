#!/bin/bash
# Ryubing-win: launch the Windows-native Ryubing from inside WSL.
#
# WSL2 only offers software (llvmpipe) Vulkan, so 3D Switch games render far
# too slowly to be playable. The Windows build of Ryubing uses the host GPU
# directly. This wrapper converts Linux paths to Windows paths and forwards
# all other arguments (e.g. --fullscreen) unchanged.
#
# Install on PATH as "Ryubing-win" (OmniCon uses it automatically on WSL):
#   sudo install -m 755 scripts/ryubing_windows.sh /usr/local/bin/Ryubing-win
#
# Point OMNICON_SWITCH_WINDOWS_EXE at your Windows Ryujinx.exe if it lives
# somewhere else (either a /mnt/... path or a Windows path works).

WIN_EXE="${OMNICON_SWITCH_WINDOWS_EXE:-/mnt/c/Users/midge/Ryubing-win/publish/Ryujinx.exe}"

# Accept a Windows-style path in the env var too.
case "$WIN_EXE" in
    [A-Za-z]:\\*|[A-Za-z]:/*)
        WIN_EXE="$(wslpath -u "$WIN_EXE")"
        ;;
esac

if [ ! -f "$WIN_EXE" ]; then
    echo "Ryubing-win: Windows Ryujinx.exe not found at: $WIN_EXE" >&2
    echo "Set OMNICON_SWITCH_WINDOWS_EXE to its location." >&2
    exit 1
fi

args=()
for a in "$@"; do
    if [ -e "$a" ]; then
        args+=("$(wslpath -w "$a")")
    else
        args+=("$a")
    fi
done

exec "$WIN_EXE" "${args[@]}"
