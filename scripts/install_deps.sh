#!/usr/bin/env bash
# OmniCon dependency bootstrapper: installs whatever is missing so a fresh
# device can build and run the project.
#
#   scripts/install_deps.sh           # build + runtime dependencies
#   scripts/install_deps.sh --build   # only what `make` needs
#   scripts/install_deps.sh --runtime # only what the emulator front-ends need
#
# Idempotent: anything already present is skipped. System packages are
# installed with apt (using sudo when needed and possible); emulators are
# downloaded as AppImages into ~/.local/bin so no root is required.

set -u
cd "$(dirname "$0")/.."

DO_BUILD=0
DO_RUNTIME=0
case "${1:-}" in
    --build)   DO_BUILD=1 ;;
    --runtime) DO_RUNTIME=1 ;;
    ""|--all)  DO_BUILD=1; DO_RUNTIME=1 ;;
    *) echo "usage: $0 [--build|--runtime|--all]" >&2; exit 2 ;;
esac

log()  { printf '[deps] %s\n' "$*"; }
warn() { printf '[deps] WARNING: %s\n' "$*" >&2; }
have() { command -v "$1" >/dev/null 2>&1; }

USER_BIN="$HOME/.local/bin"
mkdir -p "$USER_BIN"
case ":$PATH:" in
    *":$USER_BIN:"*) ;;
    *) PATH="$USER_BIN:$PATH" ;;
esac

SUDO=""
if [ "$(id -u)" -ne 0 ]; then
    if sudo -n true 2>/dev/null; then
        SUDO="sudo -n"
    elif [ -t 0 ]; then
        SUDO="sudo"
    fi
fi

APT_UPDATED=0
apt_install() {
    local want=() pkg
    for pkg in "$@"; do
        dpkg -s "$pkg" >/dev/null 2>&1 || want+=("$pkg")
    done
    if [ "${#want[@]}" -eq 0 ]; then
        return 0
    fi
    if [ -z "$SUDO" ]; then
        warn "Missing packages, install them manually: ${want[*]}"
        return 1
    fi
    log "Installing packages: ${want[*]}"
    if [ "$APT_UPDATED" -eq 0 ]; then
        $SUDO apt-get update -qq && APT_UPDATED=1
    fi
    $SUDO apt-get install -y "${want[@]}"
}

# Download the latest-release asset matching a regex from a GitHub/Gitea API
# URL and install it as an executable named $4 in ~/.local/bin.
#   $1 API URL   $2 asset regex   $3 exclusion regex   $4 installed name
install_appimage() {
    local api="$1" regex="$2" exclude="$3" name="$4" url
    log "Looking up the latest $name release..."
    url=$(curl -fsSL "$api" \
        | grep -o '"browser_download_url": *"[^"]*"' \
        | cut -d'"' -f4 \
        | grep -E "$regex" \
        | grep -Ev "$exclude" \
        | head -n1)
    if [ -z "$url" ]; then
        warn "Could not find a $name download; install it manually."
        return 1
    fi
    log "Downloading $url"
    if curl -fSL "$url" -o "$USER_BIN/$name"; then
        chmod +x "$USER_BIN/$name"
        log "Installed $name to $USER_BIN/$name"
    else
        warn "Failed to download $name; install it manually."
        return 1
    fi
}

build_cppp() {
    local tmp
    tmp=$(mktemp -d) || return 1
    log "Building cppp (SameBoy header tool)..."
    if git clone -q --depth 1 https://github.com/LIJI32/cppp.git "$tmp/cppp" \
        && make -s -C "$tmp/cppp" CC="${CC:-cc}"; then
        cp "$tmp/cppp/cppp" "$USER_BIN/"
        chmod +x "$USER_BIN/cppp"
        log "Installed cppp to $USER_BIN/cppp"
    else
        warn "Could not build cppp; libsameboy headers may be incomplete."
    fi
    rm -rf "$tmp"
}

clone_sameboy() {
    local sha
    sha=$(git ls-files -s third_party/SameBoy 2>/dev/null | awk '{print $2}')
    log "Cloning SameBoy..."
    if ! git clone https://github.com/LIJI32/SameBoy.git third_party/SameBoy; then
        warn "Could not clone SameBoy; Game Boy boot ROMs cannot be built."
        return 1
    fi
    if [ -n "$sha" ]; then
        git -C third_party/SameBoy checkout -q "$sha" \
            || warn "Could not check out the pinned SameBoy commit $sha"
    fi
}

ensure_build_deps() {
    log "Checking build dependencies..."
    apt_install build-essential git curl python3 unzip

    # Capability-based checks: only apt-install when the tool/header itself is
    # missing, so equivalent distro packages don't trigger false positives.
    have sdl2-config || apt_install libsdl2-dev
    [ -f /usr/include/SDL2/SDL_ttf.h ]   || apt_install libsdl2-ttf-dev
    [ -f /usr/include/SDL2/SDL_image.h ] || apt_install libsdl2-image-dev
    have mysql_config || apt_install default-libmysqlclient-dev

    if ! ldconfig -p 2>/dev/null | grep -q libmgba; then
        apt_install libmgba-dev \
            || warn "libmgba not found; install libmgba-dev manually."
    fi

    if ! have rgbasm; then
        apt_install rgbds \
            || warn "RGBDS not found; install rgbasm/rgblink/rgbgfx manually (https://rgbds.gbdev.io)."
    fi

    have cppp || build_cppp

    if [ ! -f third_party/SameBoy/Makefile ]; then
        clone_sameboy
    fi

    if [ -f third_party/SameBoy/Makefile ]; then
        if [ ! -f third_party/SameBoy/build/bin/BootROMs/dmg_boot.bin ]; then
            log "Building SameBoy boot ROMs..."
            make -C third_party/SameBoy bootroms \
                || warn "SameBoy boot ROM build failed."
        fi
        if [ ! -f third_party/SameBoy/build/lib/libsameboy.a ]; then
            log "Building libsameboy..."
            make -C third_party/SameBoy lib \
                || warn "libsameboy build failed."
        fi
    fi
}

ensure_runtime_deps() {
    log "Checking runtime dependencies..."
    have curl  || apt_install curl
    have mysql || apt_install default-mysql-client

    if ! have retroarch; then
        apt_install retroarch \
            || warn "RetroArch not found; install it manually."
    fi

    have cemu || install_appimage \
        "https://api.github.com/repos/cemu-project/Cemu/releases/latest" \
        'x86_64\.AppImage$' 'zsync|sha' cemu

    have Ryubing || install_appimage \
        "https://git.ryujinx.app/api/v1/repos/projects/Ryubing/releases/latest" \
        'x64\.AppImage$' 'zsync|sha|arm64' Ryubing

    have Vita3K || install_appimage \
        "https://api.github.com/repos/Vita3K/Vita3K/releases/latest" \
        'Vita3K-x86_64\.AppImage$' 'zsync|sha' Vita3K

    have xemu || install_appimage \
        "https://api.github.com/repos/xemu-project/xemu/releases/latest" \
        'x86_64\.AppImage$' 'zsync|sha|dbg' xemu

    if ! have xenia; then
        log "xenia (Xbox 360) is Windows-only; install it manually if needed."
    fi
}

[ "$DO_BUILD" -eq 1 ]   && ensure_build_deps
[ "$DO_RUNTIME" -eq 1 ] && ensure_runtime_deps
log "Done."
