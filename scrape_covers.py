#!/usr/bin/env python3
"""
scrape_covers.py — Download box art for OmniCon ROMs.

Sources:
  * libretro thumbnails CDN  — NES, SNES, GB, GBC, GBA, NDS, 3DS, Atari 2600/5200/800
  * Wikimedia Commons API    — Magnavox Odyssey
  * Steam CDN                — Steam games (requires a local Steam install)

No API key required for any source.

Usage:
    python3 scrape_covers.py [--roms-dir Roms] [--covers-dir saves/covers]
                             [--steam-root PATH] [--no-steam] [--no-odyssey]
"""

import os
import re
import sys
import json
import time
import urllib.request
import urllib.parse
import argparse

# ─── System config ────────────────────────────────────────────────────────────

SYSTEM_MAP = {
    ".nes":  ("nes",        "Nintendo - Nintendo Entertainment System"),
    ".smc":  ("snes",       "Nintendo - Super Nintendo Entertainment System"),
    ".sfc":  ("snes",       "Nintendo - Super Nintendo Entertainment System"),
    ".gb":   ("gb",         "Nintendo - Game Boy"),
    ".gbc":  ("gbc",        "Nintendo - Game Boy Color"),
    ".sgb":  ("gbc",        "Nintendo - Game Boy Color"),
    ".gba":  ("gba",        "Nintendo - Game Boy Advance"),
    ".nds":  ("nds",        "Nintendo - Nintendo DS"),
    ".3ds":  ("3ds",        "Nintendo - Nintendo 3DS"),
    ".cci":  ("3ds",        "Nintendo - Nintendo 3DS"),
    ".cxi":  ("3ds",        "Nintendo - Nintendo 3DS"),
    ".3dsx": ("3ds",        "Nintendo - Nintendo 3DS"),
    ".cia":  ("3ds",        "Nintendo - Nintendo 3DS"),
    ".a26":  ("atari2600",  "Atari - 2600"),
    ".a52":  ("atari5200",  "Atari - 5200"),
    ".atr":  ("atari800",   "Atari - 8-bit"),
    ".xfd":  ("atari800",   "Atari - 8-bit"),
    ".atx":  ("atari800",   "Atari - 8-bit"),
    ".xex":  ("atari800",   "Atari - 8-bit"),
    ".car":  ("atari800",   "Atari - 8-bit"),
    ".cas":  ("atari800",   "Atari - 8-bit"),
    ".com":  ("atari800",   "Atari - 8-bit"),
    ".md":   ("genesis",    "Sega - Mega Drive - Genesis"),
    ".gen":  ("genesis",    "Sega - Mega Drive - Genesis"),
    ".smd":  ("genesis",    "Sega - Mega Drive - Genesis"),
    ".68k":  ("genesis",    "Sega - Mega Drive - Genesis"),
    ".sgd":  ("genesis",    "Sega - Mega Drive - Genesis"),
}

LIBRETRO_BASE  = "https://thumbnails.libretro.com"
WIKIMEDIA_API  = "https://commons.wikimedia.org/w/api.php"
WIKIMEDIA_FILE = "https://commons.wikimedia.org/wiki/Special:FilePath"
STEAM_CDN      = "https://cdn.akamai.steamstatic.com/steam/apps"

# ─── Helpers ──────────────────────────────────────────────────────────────────

def clean_filename(stem: str) -> str:
    """Mirror GameLibrary::cleanFileName — strip region/version tags."""
    name = re.sub(r"\s*\([^)]*\)", "", stem)   # remove (...) tags
    name = re.sub(r"\s*\[[^\]]*\]", "", name)  # remove [...] tags
    name = name.strip(" -.")
    return name if name else stem


def libretro_url(system_long: str, stem: str) -> str:
    """Build a libretro thumbnail URL for a given system + ROM stem."""
    # libretro thumbnails use the full No-Intro stem (with region tags)
    encoded_sys  = urllib.parse.quote(system_long, safe="")
    encoded_name = urllib.parse.quote(stem + ".png", safe="")
    return f"{LIBRETRO_BASE}/{encoded_sys}/Named_Boxarts/{encoded_name}"


def download(url: str, dest: str) -> bool:
    """Fetch url → dest. Returns True on success."""
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "OmniCon/1.0"})
        with urllib.request.urlopen(req, timeout=15) as resp:
            if resp.status != 200:
                return False
            data = resp.read()
            # Reject tiny files (likely 404 HTML)
            if len(data) < 1000:
                return False
            with open(dest, "wb") as f:
                f.write(data)
        return True
    except Exception:
        return False


def fetch_json(url: str):
    """Fetch a JSON endpoint. Returns parsed dict or None on failure."""
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "OmniCon/1.0"})
        with urllib.request.urlopen(req, timeout=15) as resp:
            if resp.status != 200:
                return None
            return json.loads(resp.read().decode("utf-8"))
    except Exception:
        return None


# ─── Odyssey via Wikimedia Commons ────────────────────────────────────────────

def odyssey_bare_name(stem: str) -> str:
    """'Odyssey - Tennis' → 'Tennis'"""
    prefix = "Odyssey - "
    return stem[len(prefix):] if stem.startswith(prefix) else stem


def wikimedia_search_image(query: str) -> str:
    """
    Search Wikimedia Commons for the best matching image file.
    Returns the raw filename (without 'File:' prefix) or empty string.
    """
    params = urllib.parse.urlencode({
        "action":      "query",
        "list":        "search",
        "srsearch":    query,
        "srnamespace": 6,       # NS 6 = File
        "srlimit":     5,
        "format":      "json",
    })
    data = fetch_json(f"{WIKIMEDIA_API}?{params}")
    if not data:
        return ""
    results = data.get("query", {}).get("search", [])
    if not results:
        return ""
    title = results[0]["title"]  # e.g. "File:Magnavox Odyssey Tennis.jpg"
    if title.startswith("File:"):
        title = title[5:]
    return title


def download_odyssey_cover(game_bare: str, dest: str) -> bool:
    """
    Download a Wikimedia Commons image for a Magnavox Odyssey game.
    Tries multiple search queries in priority order.
    """
    queries = [
        f"Magnavox Odyssey {game_bare} overlay",
        f"Magnavox Odyssey {game_bare}",
        f"Odyssey {game_bare} game card",
    ]
    for q in queries:
        print(f"          wikimedia: {q!r}")
        filename = wikimedia_search_image(q)
        if not filename:
            time.sleep(0.2)
            continue
        url = f"{WIKIMEDIA_FILE}/{urllib.parse.quote(filename)}"
        print(f"          → {filename}")
        if download(url, dest):
            return True
        time.sleep(0.3)
    return False


# ─── Steam library scan ───────────────────────────────────────────────────────

def find_steam_root() -> str:
    """Locate the local Steam installation root. Returns path or empty string."""
    candidates = []
    if r := os.environ.get("STEAM_ROOT"):
        candidates.append(r)
    if home := os.environ.get("HOME"):
        for s in ("/.local/share/Steam", "/.steam/steam", "/Steam"):
            candidates.append(home + s)
    for drv in ("c", "d", "e", "f"):
        for s in ("/Program Files (x86)/Steam", "/Program Files/Steam"):
            candidates.append(f"/mnt/{drv}{s}")
    for c in candidates:
        if os.path.isdir(os.path.join(c, "steamapps")):
            return c
    return ""


def collect_steam_games(steam_root: str):
    """Yield (appid, name) tuples for fully-installed Steam games."""
    is_windows = steam_root.startswith("/mnt/")

    def to_linux_path(raw: str) -> str:
        p = ""
        i = 0
        while i < len(raw):
            if raw[i] == "\\" and i + 1 < len(raw) and raw[i + 1] == "\\":
                p += "/"; i += 2
            elif raw[i] == "\\":
                p += "/"; i += 1
            else:
                p += raw[i]; i += 1
        if len(p) >= 2 and p[0].isalpha() and p[1] == ":":
            return "/mnt/" + p[0].lower() + (p[2:] if len(p) > 2 else "/")
        return p

    lib_dirs = [steam_root + "/steamapps"]
    vdf_path = steam_root + "/steamapps/libraryfolders.vdf"
    if os.path.isfile(vdf_path):
        with open(vdf_path, encoding="utf-8", errors="replace") as f:
            vdf = f.read()
        for m in re.finditer(r'"path"\s+"([^"]+)"', vdf):
            raw = m.group(1)
            d = (to_linux_path(raw) if is_windows else raw) + "/steamapps"
            if d != lib_dirs[0] and os.path.isdir(d):
                lib_dirs.append(d)
        if len(lib_dirs) == 1:
            for m in re.finditer(r'"\d+"\s+"([^"]+)"', vdf):
                raw = m.group(1)
                d = (to_linux_path(raw) if is_windows else raw) + "/steamapps"
                if d != lib_dirs[0] and os.path.isdir(d):
                    lib_dirs.append(d)

    for lib_dir in lib_dirs:
        if not os.path.isdir(lib_dir):
            continue
        for entry in os.scandir(lib_dir):
            fname = entry.name
            if not fname.startswith("appmanifest_") or not fname.endswith(".acf"):
                continue
            try:
                with open(entry.path, encoding="utf-8", errors="replace") as f:
                    acf = f.read()
            except OSError:
                continue

            def kv(key: str, _acf=acf) -> str:
                m = re.search('"' + re.escape(key) + r'"\s+"([^"]+)"', _acf)
                return m.group(1) if m else ""

            appid     = kv("appid")
            name      = kv("name")
            flags_str = kv("StateFlags")
            if not appid or not name or not appid.isdigit():
                continue
            try:
                if (int(flags_str) & 4) == 0:
                    continue
            except (ValueError, TypeError):
                continue
            yield appid, name


def download_steam_cover(appid: str, dest: str) -> bool:
    """Try portrait art then landscape header from Steam CDN."""
    for img in ("library_600x900.jpg", "header.jpg"):
        url = f"{STEAM_CDN}/{appid}/{img}"
        print(f"          {url}")
        if download(url, dest):
            return True
        time.sleep(0.2)
    return False


# ─── Scan ROMs ────────────────────────────────────────────────────────────────

def collect_roms(roms_dir: str):
    """Yield (rom_path, ext, stem) tuples for known ROM types."""
    for entry in os.scandir(roms_dir):
        if not entry.is_file():
            continue
        name = entry.name
        # skip Zone.Identifier sidecar files
        if ":" in name or name.startswith("."):
            continue
        root, ext = os.path.splitext(name)
        ext = ext.lower()
        if ext in SYSTEM_MAP or ext == ".odyssey":
            yield entry.path, ext, root
    # also walk known subdirs
    subdirs = ["nes","snes","gb","gbc","gba","nds","3ds",
               "atari2600","atari5200","atari800","genesis","odyssey"]
    for sd in subdirs:
        sub = os.path.join(roms_dir, sd)
        if not os.path.isdir(sub):
            continue
        for entry in os.scandir(sub):
            if not entry.is_file():
                continue
            name = entry.name
            if ":" in name or name.startswith("."):
                continue
            root, ext = os.path.splitext(name)
            ext = ext.lower()
            if ext in SYSTEM_MAP or ext == ".odyssey":
                yield entry.path, ext, root


# ─── Main ─────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="Scrape cover art for OmniCon ROMs")
    parser.add_argument("--roms-dir",   default="Roms",         help="Path to ROMs folder")
    parser.add_argument("--covers-dir", default="saves/covers", help="Path to covers folder")
    parser.add_argument("--steam-root", default=None,
                        help="Steam root dir (auto-detected if omitted)")
    parser.add_argument("--no-steam",   action="store_true",    help="Skip Steam games")
    parser.add_argument("--no-odyssey", action="store_true",    help="Skip Odyssey games")
    args = parser.parse_args()

    roms_dir   = args.roms_dir
    covers_dir = args.covers_dir

    if not os.path.isdir(roms_dir):
        print(f"[error] ROMs directory not found: {roms_dir}")
        sys.exit(1)

    roms = list(collect_roms(roms_dir))
    if not roms:
        print("[info] No ROMs found.")
        return

    print(f"Found {len(roms)} ROM(s). Fetching cover art...\n")

    ok = skip = fail = 0

    # ── libretro CDN + Wikimedia (ROM files) ─────────────────────────────────
    for rom_path, ext, stem in roms:

        # ── Magnavox Odyssey via Wikimedia Commons ────────────────────────────
        if ext == ".odyssey":
            if args.no_odyssey:
                continue
            clean     = clean_filename(stem)
            bare      = odyssey_bare_name(stem)
            dest_dir  = os.path.join(covers_dir, "odyssey")
            dest_path = os.path.join(dest_dir, clean + ".png")
            if os.path.isfile(dest_path) and os.path.getsize(dest_path) > 1000:
                print(f"  [skip] {clean}")
                skip += 1
                continue
            os.makedirs(dest_dir, exist_ok=True)
            print(f"  [odyssey] {clean}")
            if download_odyssey_cover(bare, dest_path):
                kb = os.path.getsize(dest_path) // 1024
                print(f"          ✓ saved ({kb} KB)")
                ok += 1
            else:
                print(f"          ✗ not found on Wikimedia Commons")
                fail += 1
            time.sleep(0.4)
            continue

        # ── libretro thumbnails CDN ───────────────────────────────────────────
        sys_dir, sys_long = SYSTEM_MAP[ext]
        clean     = clean_filename(stem)
        dest_dir  = os.path.join(covers_dir, sys_dir)
        dest_path = os.path.join(dest_dir, clean + ".png")

        if os.path.isfile(dest_path) and os.path.getsize(dest_path) > 1000:
            print(f"  [skip] {clean}")
            skip += 1
            continue

        os.makedirs(dest_dir, exist_ok=True)
        url = libretro_url(sys_long, stem)
        print(f"  [fetch] {clean}  ({sys_long})")
        print(f"          {url}")

        if download(url, dest_path):
            size_kb = os.path.getsize(dest_path) // 1024
            print(f"          ✓ saved ({size_kb} KB)")
            ok += 1
        else:
            # Try alternate: clean name (no region tags)
            url2 = libretro_url(sys_long, clean)
            print(f"          retry → {url2}")
            if download(url2, dest_path):
                size_kb = os.path.getsize(dest_path) // 1024
                print(f"          ✓ saved via clean name ({size_kb} KB)")
                ok += 1
            else:
                print(f"          ✗ not found")
                fail += 1

        time.sleep(0.3)  # polite rate limit

    # ── Steam CDN ────────────────────────────────────────────────────────────
    if not args.no_steam:
        steam_root = args.steam_root or find_steam_root()
        if not steam_root:
            print("\n[steam] No local Steam installation found; skipping.")
        else:
            print(f"\n[steam] Root: {steam_root}")
            steam_games = list(collect_steam_games(steam_root))
            if not steam_games:
                print("[steam] No installed Steam games found.")
            else:
                print(f"[steam] Found {len(steam_games)} installed game(s).\n")
                steam_covers_dir = os.path.join(covers_dir, "steam")
                os.makedirs(steam_covers_dir, exist_ok=True)
                for appid, name in steam_games:
                    dest = os.path.join(steam_covers_dir, f"{appid}.jpg")
                    if os.path.isfile(dest) and os.path.getsize(dest) > 1000:
                        print(f"  [skip] {name} ({appid})")
                        skip += 1
                        continue
                    print(f"  [steam] {name} ({appid})")
                    if download_steam_cover(appid, dest):
                        kb = os.path.getsize(dest) // 1024
                        print(f"          ✓ saved ({kb} KB)")
                        ok += 1
                    else:
                        print(f"          ✗ not found")
                        fail += 1
                    time.sleep(0.3)

    print(f"\nDone.  ✓ {ok} downloaded  •  {skip} already cached  •  {fail} not found")
    if fail > 0:
        print("(Missing covers will show as coloured placeholder tiles in the shell.)")


if __name__ == "__main__":
    main()
