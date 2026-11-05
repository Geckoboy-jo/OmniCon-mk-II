#!/usr/bin/env bash
# OmniCon dependency bootstrapper: installs whatever is missing so a fresh
# device can build and run the project.
#
#   scripts/install_deps.sh           # build + runtime dependencies
#   scripts/install_deps.sh --build   # only what `make` needs
#   scripts/install_deps.sh --runtime # only what the emulator front-ends need
#   scripts/install_deps.sh --xenia   # only the Xbox 360 emulator (xenia_canary)
#
# Idempotent: anything already present is skipped. System packages are
# installed with apt (using sudo when needed and possible); emulators are
# downloaded as AppImages into ~/.local/bin so no root is required.

set -u
cd "$(dirname "$0")/.."

DO_BUILD=0
DO_RUNTIME=0
DO_XENIA=0
case "${1:-}" in
    --build)   DO_BUILD=1 ;;
    --runtime) DO_RUNTIME=1 ;;
    --xenia)   DO_XENIA=1 ;;
    ""|--all)  DO_BUILD=1; DO_RUNTIME=1 ;;
    *) echo "usage: $0 [--build|--runtime|--xenia|--all]" >&2; exit 2 ;;
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
    if [ -z "$SUDO" ] && [ "$(id -u)" -ne 0 ]; then
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

# Xenia (Xbox 360): the xenia-canary experimental Linux build is a tarball, so
# unpack it under ~/.local/share and link its executable into ~/.local/bin.
install_xenia_canary() {
    local api="https://api.github.com/repos/xenia-canary/xenia-canary-releases/releases/latest"
    local url tmp dir bin glibc
    # The prebuilt Linux binary needs glibc 2.38+ (Ubuntu 24.04+); older systems
    # would only get a binary that cannot start.
    glibc=$(ldd --version 2>/dev/null | head -n1 | grep -oE '[0-9]+\.[0-9]+$')
    if [ -n "$glibc" ] && [ "$(printf '%s\n2.38\n' "$glibc" | sort -V | head -n1)" != "2.38" ]; then
        warn "xenia_canary needs glibc 2.38+ (this system has $glibc); upgrade to Ubuntu 24.04+ or set OMNICON_XBOX360_LAUNCHER."
        return 1
    fi
    have xz || apt_install xz-utils
    apt_install libsdl2-2.0-0 libvulkan1 mesa-vulkan-drivers libgtk-3-0t64 \
        || apt_install libsdl2-2.0-0 libvulkan1 mesa-vulkan-drivers libgtk-3-0 \
        || warn "Could not install xenia_canary's runtime libraries (SDL2, Vulkan, GTK)."
    apt_install libasound2t64 || apt_install libasound2 \
        || warn "Could not install libasound2 (needed by xenia_canary)."
    log "Looking up the latest xenia_canary Linux build..."
    url=$(curl -fsSL "$api" \
        | grep -o '"browser_download_url": *"[^"]*"' \
        | cut -d'"' -f4 \
        | grep -E 'linux.*\.tar.*\.xz$' \
        | head -n1)
    if [ -z "$url" ]; then
        warn "Could not find a xenia_canary Linux download; install it manually."
        return 1
    fi
    tmp=$(mktemp --suffix=.tar.xz) || return 1
    dir="$HOME/.local/share/xenia_canary"
    log "Downloading $url"
    if curl -fSL "$url" -o "$tmp" \
        && rm -rf "$dir" && mkdir -p "$dir" \
        && tar -xJf "$tmp" -C "$dir"; then
        bin=$(find "$dir" -type f -perm -u+x -name 'xenia_canary*' ! -name '*.so*' | head -n1)
        if [ -n "$bin" ]; then
            ln -sf "$bin" "$USER_BIN/xenia_canary"
            log "Installed xenia_canary to $USER_BIN/xenia_canary"
        else
            warn "Unpacked xenia_canary to $dir but found no executable; link it manually."
        fi
    else
        warn "Failed to install xenia_canary; install it manually."
    fi
    rm -f "$tmp"
}

# Some distros (e.g. Ubuntu 24.04) do not package RGBDS; use the upstream
# prebuilt Linux release and link its tools into ~/.local/bin.
install_rgbds_release() {
    local url tmp dir tool
    have xz || apt_install xz-utils
    url=$(curl -fsSL "https://api.github.com/repos/gbdev/rgbds/releases/latest" \
        | grep -o '"browser_download_url": *"[^"]*"' \
        | cut -d'"' -f4 \
        | grep -E 'rgbds-linux-x86_64\.tar\.xz$' \
        | head -n1)
    if [ -z "$url" ]; then
        warn "Could not find an RGBDS download."
        return 1
    fi
    tmp=$(mktemp --suffix=.tar.xz) || return 1
    dir="$HOME/.local/share/rgbds"
    log "Downloading $url"
    if curl -fSL "$url" -o "$tmp" && rm -rf "$dir" && mkdir -p "$dir" \
        && tar -xJf "$tmp" -C "$dir"; then
        for tool in rgbasm rgblink rgbfix rgbgfx; do
            ln -sf "$(find "$dir" -type f -name "$tool" | head -n1)" "$USER_BIN/$tool"
        done
        log "Installed RGBDS to $USER_BIN"
    fi
    rm -f "$tmp"
    have rgbasm
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
    apt_install libopenxr-dev
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
        apt_install rgbds || install_rgbds_release \
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

steam_roots() {
    local drv sub
    [ -n "${STEAM_ROOT:-}" ] && echo "$STEAM_ROOT"
    echo "$HOME/.local/share/Steam"
    echo "$HOME/.steam/steam"
    for drv in c d e f; do
        for sub in "Program Files (x86)" "Program Files"; do
            echo "/mnt/$drv/$sub/Steam"
        done
    done
}

# Windows Steam reachable through a WSL mount; empty when /mnt has none.
windows_steam_root() {
    local root
    while IFS= read -r root; do
        case "$root" in
            /mnt/*) [ -d "$root/steamapps" ] && { echo "$root"; return 0; } ;;
        esac
    done < <(steam_roots)
    return 1
}

steamvr_installed() {
    local root
    while IFS= read -r root; do
        [ -d "$root/steamapps/common/SteamVR" ] && return 0
    done < <(steam_roots)
    return 1
}



# Steam comes from Valve's official .deb (needs i386 multiarch for the client).
install_steam() {
    local deb
    if [ -z "$SUDO" ] && [ "$(id -u)" -ne 0 ]; then
        warn "Steam needs root to install; run: sudo apt install steam-installer"
        return 1
    fi
    deb=$(mktemp --suffix=.deb) || return 1
    log "Downloading Steam..."
    if curl -fSL "https://cdn.cloudflare.steamstatic.com/client/installer/steam.deb" -o "$deb"; then
        $SUDO dpkg --add-architecture i386
        $SUDO apt-get update -qq && APT_UPDATED=1
        $SUDO apt-get install -y "$deb" || warn "Steam installation failed."
    else
        warn "Failed to download Steam; install it manually."
    fi
    rm -f "$deb"
    have steam
}

# SteamVR (appid 250820) is a Steam app, so it is installed through the Steam
# client; Steam must be able to log in, so this only queues the install.
# Under WSL with Windows Steam on /mnt the Windows client is used; otherwise
# the Linux client.
install_steamvr() {
    log "Asking Steam to install SteamVR (confirm in the Steam window if prompted)..."
    if windows_steam_root >/dev/null && have cmd.exe; then
        cmd.exe /c start "" steam://install/250820 >/dev/null 2>&1 &
    elif have steam; then
        nohup steam steam://install/250820 >/dev/null 2>&1 &
    else
        warn "Steam is unavailable; install SteamVR manually."
    fi
}
install_alvr() {
    log "Installing ALVR..."

    have flatpak || apt_install flatpak

    flatpak remote-add --if-not-exists flathub \
        https://flathub.org/repo/flathub.flatpakrepo

    if ! flatpak list | grep -q "org.alvr.ALVR"; then
        flatpak install --noninteractive flathub org.alvr.ALVR \
            || warn "Failed to install ALVR."
    fi

    apt_install monado-service || true

    log "ALVR installed."
}

# Minecraft Launcher (Java Edition). Native Linux build via the Flatpak; the
# .deb from minecraft.net is an alternative but needs sudo and multiarch.
install_minecraft() {
    if have minecraft-launcher; then
        return 0
    fi
    log "Installing the Minecraft Launcher..."
    have flatpak || apt_install flatpak
    flatpak remote-add --if-not-exists flathub \
        https://flathub.org/repo/flathub.flatpakrepo
    if ! flatpak list | grep -q "com.mojang.Minecraft"; then
        flatpak install --noninteractive flathub com.mojang.Minecraft \
            || warn "Failed to install the Minecraft Launcher."
    fi
    log "Minecraft Launcher installed."
}

# Epic Games Launcher has no native Linux build, so install Heroic Games
# Launcher (open-source Epic/GOG front-end) as the way to run it.
install_epic() {
    if have heroic; then
        return 0
    fi
    log "Installing Heroic Games Launcher (for Epic Games)..."
    have flatpak || apt_install flatpak
    flatpak remote-add --if-not-exists flathub \
        https://flathub.org/repo/flathub.flatpakrepo
    if ! flatpak list | grep -q "com.heroicgameslauncher.hgl"; then
        flatpak install --noninteractive flathub com.heroicgameslauncher.hgl \
            || warn "Failed to install Heroic Games Launcher."
    fi
    log "Heroic Games Launcher installed."
}

ensure_steam_deps() {
    if windows_steam_root >/dev/null; then
        log "Using Windows Steam at $(windows_steam_root)"
    elif ! have steam; then
        log "No Steam found (no Windows Steam under /mnt); installing the Linux client."
        install_steam || warn "Steam not installed; Steam games and SteamVR are unavailable."
    fi
    if ! steamvr_installed && { windows_steam_root >/dev/null || have steam; }; then
        install_steamvr
    fi
}

ensure_runtime_deps() {
    log "Checking runtime dependencies..."
    have curl  || apt_install curl
    have mysql || apt_install default-mysql-client




    
    if ! have flatpak || ! flatpak list | grep -q "org.alvr.ALVR"; then
    install_alvr
    fi

    # Game launchers with their own tabs in the shell.
    install_minecraft
    install_epic

    # AppImage emulators need FUSE 2; unzip/7z are used to unpack downloaded ROMs.
    apt_install libfuse2t64 || apt_install libfuse2 \
        || warn "libfuse2 missing; AppImage emulators may not start."
    have unzip || apt_install unzip
    have 7z || have 7zz || have 7zr || apt_install 7zip || apt_install p7zip-full \
        || warn "7z not found; .7z/.rar archives fall back to Python py7zr."

    ensure_steam_deps

    # WSL2: the Windows->uinput controller bridge needs evdev + uinput access.
    if grep -qi microsoft /proc/version 2>/dev/null && [ -f scripts/setup_wsl_controller.sh ]; then
        log "Setting up the WSL controller bridge..."
        bash scripts/setup_wsl_controller.sh \
            || warn "WSL controller bridge setup failed; run scripts/setup_wsl_controller.sh."
    fi

    # libretro cores for the extra consoles (skips any already present).
    if [ -f scripts/fetch_emulators.sh ]; then
        log "Fetching libretro cores..."
        # The script cds relative to its own path, so run a CR-stripped copy that
        # sits next to it (it may have been checked out with CRLF line endings).
        tr -d '\r' < scripts/fetch_emulators.sh > scripts/.fetch_emulators.run.sh
        sh scripts/.fetch_emulators.run.sh \
            || warn "Some libretro cores could not be fetched."
        rm -f scripts/.fetch_emulators.run.sh
    fi

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

    have xenia_canary || install_xenia_canary

    if [ "$(uname -s)" = "Linux" ] && ! have xenia_canary; then
        log "Xbox 360 games need Xenia; install xenia_canary manually or set OMNICON_XBOX360_LAUNCHER."
    fi
}

[ "$DO_BUILD" -eq 1 ]   && ensure_build_deps
[ "$DO_RUNTIME" -eq 1 ] && ensure_runtime_deps
[ "$DO_XENIA" -eq 1 ]   && { have curl || apt_install curl; have xenia_canary || install_xenia_canary; }
log "Done."
