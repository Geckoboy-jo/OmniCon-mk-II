import socket
"""
scrape_covers.py — Download box art for OmniCon ROMs.

Sources:
    * libretro thumbnails CDN  — NES, SNES, GB, GBC, GBA, NDS, 3DS, Atari 2600/5200/800
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
import subprocess
import time
import concurrent.futures
import threading
import urllib.request
import urllib.parse
import argparse

try:
    import requests as _requests
    from requests.adapters import HTTPAdapter
    from urllib3.util.retry import Retry
    _REQUESTS_AVAILABLE = True
except ImportError:
    _REQUESTS_AVAILABLE = False

# Optional MySQL driver (used only for --db mode).
# Falls back to the system `mysql` CLI client if no Python driver is installed.
try:
    import mysql.connector as _mysql_driver
    def _mysql_connect(host, port, user, password, database):
        return _mysql_driver.connect(
            host=host, port=port, user=user, password=password,
            database=database, connection_timeout=10)
    _MYSQL_AVAILABLE = True
except ImportError:
    try:
        import pymysql as _mysql_driver
        def _mysql_connect(host, port, user, password, database):
            return _mysql_driver.connect(
                host=host, port=port, user=user, password=password,
                database=database, connect_timeout=10)
        _MYSQL_AVAILABLE = True
    except ImportError:
        _MYSQL_AVAILABLE = False

try:
    from PIL import Image as _PIL_Image
    _PIL_AVAILABLE = True
except ImportError:
    _PIL_AVAILABLE = False

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
    ".z64":  ("n64",        "Nintendo - Nintendo 64"),
    ".n64":  ("n64",        "Nintendo - Nintendo 64"),
    ".v64":  ("n64",        "Nintendo - Nintendo 64"),
    ".cue":  ("ps1",        "Sony - PlayStation"),
    ".iso":  ("ps1",        "Sony - PlayStation"),
    ".pbp":  ("ps1",        "Sony - PlayStation"),
    ".chd":  ("ps1",        "Sony - PlayStation"),
    ".img":  ("ps1",        "Sony - PlayStation"),
    ".mdf":  ("ps1",        "Sony - PlayStation"),
}

LIBRETRO_BASE  = "https://thumbnails.libretro.com"
WIKIMEDIA_API  = "https://commons.wikimedia.org/w/api.php"
WIKIMEDIA_FILE = "https://commons.wikimedia.org/wiki/Special:FilePath"
STEAM_CDN      = "https://cdn.akamai.steamstatic.com/steam/apps"

# Maps GameLibrary::systemName() strings → (sys_dir, libretro_long_name)
# Used to look up covers for DB entries by their SYSTEM_NAME column value.
DB_SYSTEM_MAP = {
    "NES":              ("nes",       "Nintendo - Nintendo Entertainment System"),
    "SNES":             ("snes",      "Nintendo - Super Nintendo Entertainment System"),
    "Game Boy":         ("gb",        "Nintendo - Game Boy"),
    "Game Boy Color":   ("gbc",       "Nintendo - Game Boy Color"),
    "GBA":              ("gba",       "Nintendo - Game Boy Advance"),
    "Nintendo DS":      ("nds",       "Nintendo - Nintendo DS"),
    "Nintendo 3DS":     ("3ds",       "Nintendo - Nintendo 3DS"),
    "Atari 2600":       ("atari2600", "Atari - 2600"),
    "Atari 5200":       ("atari5200", "Atari - 5200"),
    "Atari 800":        ("atari800",  "Atari - 8-bit"),
    "Sega Genesis":     ("genesis",   "Sega - Mega Drive - Genesis"),
    "Nintendo 64":      ("n64",       "Nintendo - Nintendo 64"),
    "PlayStation":       ("ps1",       "Sony - PlayStation"),
    "Magnavox Odyssey": ("odyssey",   None),   # handled separately
    "Steam":            ("steam",     None),   # handled via Steam CDN (needs appid)
}

# ─── Helpers ──────────────────────────────────────────────────────────────────

def clean_filename(stem: str) -> str:
    """Mirror GameLibrary::cleanFileName — strip region/version tags."""
    name = re.sub(r"\s*\([^)]*\)", "", stem)   # remove (...) tags
    name = re.sub(r"\s*\[[^\]]*\]", "", name)  # remove [...] tags
    name = name.strip(" -.")
    return name if name else stem


def _cover_cached(path: str) -> bool:
    """Return True if a cover file exists and is larger than 1 KB."""
    try:
        return os.path.getsize(path) > 1000
    except OSError:
        return False


def libretro_url(system_long: str, stem: str, thumb_type: str = "Named_Boxarts") -> str:
    """Build a libretro thumbnail URL for a given system + ROM stem."""
    encoded_sys  = urllib.parse.quote(system_long, safe="")
    encoded_name = urllib.parse.quote(stem + ".png", safe="")
    return f"{LIBRETRO_BASE}/{encoded_sys}/{thumb_type}/{encoded_name}"


def _no_intro_the(name: str) -> str:
    """Return the No-Intro 'The' suffix form, or '' if not applicable.
    e.g. 'The Legend of Zelda' → 'Legend of Zelda, The'
    """
    if name.lower().startswith("the "):
        return name[4:] + ", The"
    return ""


def wikipedia_thumbnail(game_name: str) -> str:
    """Return the main image URL for a Wikipedia article, or ''
    Uses the free Wikipedia REST API (no key required).
    """
    encoded = urllib.parse.quote(game_name.replace(" ", "_"), safe="")
    data = fetch_json(f"https://en.wikipedia.org/api/rest_v1/page/summary/{encoded}")
    if not data:
        return ""
    return data.get("thumbnail", {}).get("source", "")


def odyssey_bare_name(stem: str) -> str:
    """'Odyssey - Tennis' → 'Tennis'"""
    prefix = "Odyssey - "
    return stem[len(prefix):] if stem.startswith(prefix) else stem


def wikimedia_search_image(query: str) -> str:
    """Search Wikimedia Commons for the best matching image filename."""
    params = urllib.parse.urlencode({
        "action":      "query",
        "list":        "search",
        "srsearch":    query,
        "srnamespace": 6,
        "srlimit":     5,
        "format":      "json",
    })
    data = fetch_json(f"{WIKIMEDIA_API}?{params}")
    if not data:
        return ""
    results = data.get("query", {}).get("search", [])
    if not results:
        return ""
    title = results[0].get("title", "")
    if title.startswith("File:"):
        title = title[5:]
    return title


def download_odyssey_cover(game_bare: str, dest: str) -> bool:
    """Download a Wikimedia Commons image for a Magnavox Odyssey game."""
    queries = [
        f"Magnavox Odyssey {game_bare} overlay",
        f"Magnavox Odyssey {game_bare}",
    ]
    for query in queries:
        print(f"          wikimedia: {query!r}")
        filename = wikimedia_search_image(query)
        if not filename:
            continue
        url = f"{WIKIMEDIA_FILE}/{urllib.parse.quote(filename)}"
        print(f"          → {filename}")
        if download_as_jpeg(url, dest):
            return True
    return False






# ─── Per-thread HTTP session (connection pooling) ───────────────────────────

_tls = threading.local()


def _get_session() -> "_requests.Session":
    """Return a thread-local requests.Session with keep-alive + retry."""
    if not hasattr(_tls, "session"):
        session = _requests.Session()
        retry = Retry(
            total=1,
            backoff_factor=0.2,
            status_forcelist=(429, 500, 502, 503, 504),
            allowed_methods=frozenset(["GET", "HEAD"]),
            raise_on_status=False,
        )
        adapter = HTTPAdapter(pool_connections=4, pool_maxsize=32, max_retries=retry)
        session.mount("https://", adapter)
        session.mount("http://",  adapter)
        session.headers["User-Agent"] = "OmniCon/1.0"
        _tls.session = session
    return _tls.session


def download(url: str, dest: str) -> bool:
    """Fetch url → dest. Returns True on success."""
    try:
        if _REQUESTS_AVAILABLE:
            resp = _get_session().get(url, timeout=(4, 7))
            if resp.status_code != 200:
                return False
            data = resp.content
        else:
            req = urllib.request.Request(url, headers={"User-Agent": "OmniCon/1.0"})
            with urllib.request.urlopen(req, timeout=7) as r:
                if r.status != 200:
                    return False
                data = r.read()
        # Reject tiny files (likely 404 HTML)
        if len(data) < 1000:
            return False
        with open(dest, "wb") as f:
            f.write(data)
        return True
    except Exception:
        return False


def download_as_jpeg(url: str, dest: str, quality: int = 85) -> bool:
    """Download url, convert to JPEG, and save to dest. Returns True on success."""
    import tempfile
    tmp_fd, tmp_path = tempfile.mkstemp(suffix=".tmp")
    os.close(tmp_fd)
    try:
        if not download(url, tmp_path):
            return False
        if _PIL_AVAILABLE:
            try:
                with _PIL_Image.open(tmp_path) as img:
                    if img.mode in ("RGBA", "LA", "P"):
                        img = img.convert("RGB")
                    img.save(dest, "JPEG", quality=quality, optimize=True)
                return True
            except Exception:
                pass
        # Fallback: copy raw bytes (no format conversion possible without Pillow)
        import shutil
        shutil.copy2(tmp_path, dest)
        return True
    except Exception:
        return False
    finally:
        try:
            os.unlink(tmp_path)
        except OSError:
            pass


def convert_existing_covers(covers_dir: str) -> None:
    """Walk covers_dir and convert any existing .png files to .jpg, removing originals."""
    if not _PIL_AVAILABLE:
        print("[convert] Pillow not installed — skipping existing PNG → JPEG conversion.")
        print("[convert] Install with: pip install Pillow")
        return
    if not os.path.isdir(covers_dir):
        return
    converted = skipped = errors = 0
    for root, _dirs, files in os.walk(covers_dir):
        for fname in files:
            if not fname.lower().endswith(".png"):
                continue
            src = os.path.join(root, fname)
            dest = os.path.join(root, fname[:-4] + ".jpg")
            if os.path.isfile(dest) and os.path.getsize(dest) > 1000:
                skipped += 1
                try:
                    os.unlink(src)
                except OSError:
                    pass
                continue
            try:
                with _PIL_Image.open(src) as img:
                    if img.mode in ("RGBA", "LA", "P"):
                        img = img.convert("RGB")
                    img.save(dest, "JPEG", quality=85, optimize=True)
                os.unlink(src)
                converted += 1
            except Exception as exc:
                print(f"  [convert] ERROR {src}: {exc}")
                errors += 1
    if converted or skipped or errors:
        msg = f"[convert] {converted} PNG(s) → JPEG"
        if skipped:
            msg += f", {skipped} already converted (PNG removed)"
        if errors:
            msg += f", {errors} error(s)"
        print(msg + ".")


def fetch_json(url: str):
    """Fetch a JSON endpoint. Returns parsed dict or None on failure."""
    try:
        if _REQUESTS_AVAILABLE:
            resp = _get_session().get(url, timeout=(4, 10))
            if resp.status_code != 200:
                return None
            return resp.json()
        req = urllib.request.Request(url, headers={"User-Agent": "OmniCon/1.0"})
        with urllib.request.urlopen(req, timeout=10) as resp:
            if resp.status != 200:
                return None
            return json.loads(resp.read().decode("utf-8"))
    except Exception:
        return None

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
    return False


# ─── Unknown-system games via archive.org ───────────────────────

def archive_identifier_from_url(url: str) -> str:
    """Extract archive.org item identifier from a download URL."""
    m = re.search(r'archive\.org/download/([^/]+)/', url)
    return m.group(1) if m else ""


def download_archive_thumbnail(identifier: str, dest: str) -> bool:
    """Download the archive.org item thumbnail to dest."""
    url = f"https://archive.org/services/img/{urllib.parse.quote(identifier)}"
    return download(url, dest)


def scrape_unknown_from_csv(csv_path: str, covers_dir: str):
    """Scrape covers for Unknown-system games listed in a CSV (name,download_url)."""
    import csv as _csv
    with open(csv_path, newline='', encoding='utf-8') as f:
        rows = [r for r in _csv.reader(f) if len(r) >= 2]
    print(f"Scraping covers for {len(rows)} unknown-system game(s)...\n")
    ok = skip = fail = 0
    dest_dir = os.path.join(covers_dir, "unknown")
    os.makedirs(dest_dir, exist_ok=True)
    for row in rows:
        name, url = row[0].strip(), row[1].strip()
        clean = clean_filename(name) or name
        dest_path = os.path.join(dest_dir, clean + ".jpg")
        if os.path.isfile(dest_path) and os.path.getsize(dest_path) > 1000:
            print(f"  [skip] {clean}")
            skip += 1
            continue
        print(f"  [unknown] {clean}")
        # 1. Try archive.org thumbnail (free, no API key)
        ident = archive_identifier_from_url(url)
        if ident:
            print(f"          archive.org id: {ident}")
            if download_archive_thumbnail(ident, dest_path):
                kb = os.path.getsize(dest_path) // 1024
                print(f"          ✓ saved ({kb} KB)")
                ok += 1
                time.sleep(0.3)
                continue
        # 2. Fallback: Wikipedia article thumbnail by game name
        wiki_img = wikipedia_thumbnail(clean)
        if wiki_img and download_as_jpeg(wiki_img, dest_path):
            kb = os.path.getsize(dest_path) // 1024
            print(f"          ✓ saved ({kb} KB)")
            ok += 1
            time.sleep(0.4)
            continue
        # 3. Fallback: Wikimedia Commons search
        filename = wikimedia_search_image(clean + " game")
        if filename:
            wurl = f"{WIKIMEDIA_FILE}/{urllib.parse.quote(filename)}"
            if download_as_jpeg(wurl, dest_path):
                kb = os.path.getsize(dest_path) // 1024
                print(f"          ✓ saved ({kb} KB)")
                ok += 1
                time.sleep(0.4)
                continue
        print(f"          ✗ not found")
        fail += 1
        time.sleep(0.3)
    print(f"\nDone.  ✓ {ok} downloaded  •  {skip} already cached  •  {fail} not found")


# ─── DB game collection ───────────────────────────────────────────────────────

def _load_omnicon_env():
    """Parse ~/.oci/omnicon_env (export KEY=VALUE) and inject into os.environ."""
    home = os.environ.get("HOME", "")
    path = os.path.join(home, ".oci", "omnicon_env")
    print(f"[debug] Loading env from: {path}")
    if not os.path.isfile(path):
        print(f"[debug] Env file not found: {path}")
        return
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            if line.startswith("export "):
                line = line[7:]
            eq = line.find("=")
            if eq < 0:
                continue
            key, val = line[:eq], line[eq + 1:]
            if len(val) >= 2 and val[0] in ('"', "'") and val[-1] == val[0]:
                val = val[1:-1]
            # Always set the variable, even if already present or empty
            os.environ[key] = val
            print(f"[debug] Set {key}={val}")


def _collect_db_games_via_driver(host, port, user, password, service):
    """Query GAMES using a Python MySQL driver."""
    conn   = _mysql_connect(host, port, user, password, service)
    cursor = conn.cursor()
    cursor.execute(
        "SELECT ID, NAME, SYSTEM_NAME, LOCAL_PATH, DOWNLOAD_URL "
        "FROM GAMES WHERE STATUS != 'failed'"
    )
    rows = cursor.fetchall()
    cursor.close()
    conn.close()
    return [
        {
            "id":           row[0],
            "name":         row[1] or "",
            "system_name":  row[2] or "",
            "local_path":   row[3] or "",
            "download_url": row[4] or "",
        }
        for row in rows
    ]


def _collect_db_games_via_cli(host, port, user, password, service):
    """
    Query GAMES using the system `mysql` CLI client.
    Used as a fallback when no Python MySQL driver is installed.
    """
    sql = ("SELECT ID, NAME, SYSTEM_NAME, LOCAL_PATH, DOWNLOAD_URL "
           "FROM GAMES WHERE STATUS != 'failed';")
    cmd = [
        "mysql",
        f"--host={host}",
        f"--port={port}",
        f"--user={user}",
        f"--password={password}",
        "--batch",          # tab-separated output, no borders
        "--skip-column-names",
        service,
        "--execute", sql,
    ]
    try:
        out = subprocess.check_output(cmd, stderr=subprocess.PIPE, timeout=15)
    except FileNotFoundError:
        print("[db] `mysql` CLI not found and no Python driver installed — cannot query DB.")
        return []
    except subprocess.CalledProcessError as exc:
        print(f"[db] mysql CLI error: {exc.stderr.decode(errors='replace').strip()}")
        return []

    result = []
    for line in out.decode(errors="replace").splitlines():
        parts = line.split("\t")
        if len(parts) < 5:
            continue
        result.append({
            "id":           parts[0],
            "name":         parts[1] if parts[1] != "NULL" else "",
            "system_name":  parts[2] if parts[2] != "NULL" else "",
            "local_path":   parts[3] if parts[3] != "NULL" else "",
            "download_url": parts[4] if parts[4] != "NULL" else "",
        })
    return result


def collect_db_games():
    """
    Query the GAMES table and return a list of dicts with keys:
      id, name, system_name, local_path, download_url
    Skips status='failed' rows (same as GameLibrary::loadFromDB).
    Tries a Python MySQL driver first; falls back to the `mysql` CLI client.
    """
    # Hardcode ORACLE_APP_HOST to match the shell environment if present
    # Always hardcode ORACLE_APP_HOST to 147.224.62.87
    os.environ["ORACLE_APP_HOST"] = "147.224.62.87"
    print(f"[debug] Hardcoded ORACLE_APP_HOST to 147.224.62.87")
    _load_omnicon_env()

    host     = os.environ.get("ORACLE_APP_HOST",     "")
    port     = int(os.environ.get("ORACLE_APP_PORT", "13306"))
    user     = os.environ.get("ORACLE_APP_USER",     "omnicon")
    password = "Geckoboy07!1word"  # Hardcoded password as requested
    service  = os.environ.get("ORACLE_APP_SERVICE",  "omnicon")
    if service in ("XE", "FREE", ""):
        service = "omnicon"

    if not host:
        print("[db] ORACLE_APP_HOST not set — skipping DB scrape.")
        return []

    if _MYSQL_AVAILABLE:
        try:
            return _collect_db_games_via_driver(host, port, user, password, service)
        except Exception as exc:
            print(f"[db] Python driver failed ({exc}), falling back to CLI.")

    return _collect_db_games_via_cli(host, port, user, password, service)


def _db_game_dest_path(g: dict, covers_dir: str) -> str:
    """Return the expected cover-file path for a DB game entry, or '' if undetermined."""
    name        = g.get("name", "")
    system_name = g.get("system_name", "")
    local_path  = g.get("local_path", "")
    if not name:
        return ""
    if local_path and os.path.isfile(local_path):
        stem = os.path.splitext(os.path.basename(local_path))[0]
    else:
        decoded = urllib.parse.unquote(name)
        stem_candidate, ext_candidate = os.path.splitext(decoded)
        _archive_exts = {".zip", ".7z", ".gz", ".bz2", ".xz", ".rar"}
        _rom_exts = set(SYSTEM_MAP.keys()) | {".odyssey"}
        if ext_candidate.lower() in _archive_exts:
            inner, inner_ext = os.path.splitext(stem_candidate)
            stem = inner if inner_ext.lower() in _rom_exts else stem_candidate
        elif ext_candidate.lower() in _rom_exts:
            stem = stem_candidate
        else:
            stem = decoded
    clean = clean_filename(stem)
    if system_name == "Magnavox Odyssey":
        return os.path.join(covers_dir, "odyssey", clean + ".jpg")
    if system_name == "Steam":
        return ""  # Steam games not fetched via DB scraper
    mapping = DB_SYSTEM_MAP.get(system_name)
    if mapping and mapping[1] is not None:
        return os.path.join(covers_dir, mapping[0], clean + ".jpg")
    return os.path.join(covers_dir, "unknown", clean + ".jpg")


def scrape_db_covers(db_games: list, covers_dir: str, no_odyssey: bool = False,
                     no_wiki: bool = False, max_workers: int = 32):
    """Download missing cover art for games that came from the Oracle/MySQL DB."""
    if not db_games:
        print("[db] No DB games to scrape.")
        return

    # Pre-filter: only dispatch workers for games that are missing covers
    pending = []
    pre_skip = 0
    for g in db_games:
        if no_odyssey and g.get("system_name") == "Magnavox Odyssey":
            pre_skip += 1
            continue
        dest = _db_game_dest_path(g, covers_dir)
        if dest and _cover_cached(dest):
            pre_skip += 1
        else:
            pending.append(g)

    if not pending:
        print(f"[db] All {len(db_games)} DB cover(s) already cached.")
        return

    print(f"[db] {len(db_games)} DB game(s), {len(pending)} missing cover(s). Fetching...")
    ok = skip = fail = 0
    counter = [0]
    lock    = threading.Lock()
    total   = len(pending)
    tasks   = [(g, covers_dir, no_odyssey, no_wiki, counter, total, lock) for g in pending]

    with concurrent.futures.ThreadPoolExecutor(max_workers=max_workers) as pool:
        results = list(pool.map(_scrape_db_game_worker, tasks))

    ok   = results.count("ok")
    skip = results.count("skip") + pre_skip
    fail = results.count("fail")

    print(f"[db] Done.  \u2713 {ok} downloaded  \u2022  {skip} already cached  \u2022  {fail} not found\n")


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
               "atari2600","atari5200","atari800","genesis","n64","odyssey"]
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

# ─── Thread-pool workers ──────────────────────────────────────────────────────────
def _scrape_rom_worker(task):
    """Scrape cover art for one ROM file (called by ThreadPoolExecutor)."""
    rom_path, ext, stem, covers_dir, no_odyssey, no_wiki, counter, total, lock = task
    clean = clean_filename(stem)
    with lock:
        counter[0] += 1
        idx = counter[0]
    print(f"  scraping {clean} ... {idx}/{total}")

    if ext == ".odyssey":
        if no_odyssey:
            return "skip"
        dest_dir  = os.path.join(covers_dir, "odyssey")
        dest_path = os.path.join(dest_dir, clean + ".jpg")
        if os.path.isfile(dest_path) and os.path.getsize(dest_path) > 1000:
            return "skip"
        os.makedirs(dest_dir, exist_ok=True)
        bare = odyssey_bare_name(stem)
        if download_odyssey_cover(bare, dest_path):
            kb = os.path.getsize(dest_path) // 1024
            print(f"          ✓ {clean} ({kb} KB)")
            return "ok"
        print(f"          ✗ {clean} not found on Wikimedia Commons")
        return "fail"

    sys_dir, sys_long = SYSTEM_MAP[ext]
    dest_dir  = os.path.join(covers_dir, sys_dir)
    dest_path = os.path.join(dest_dir, clean + ".jpg")
    if os.path.isfile(dest_path) and os.path.getsize(dest_path) > 1000:
        return "skip"
    os.makedirs(dest_dir, exist_ok=True)
    # 1. Try libretro boxart (main)
    url = libretro_url(sys_long, stem)
    if download_as_jpeg(url, dest_path):
        kb = os.path.getsize(dest_path) // 1024
        print(f"          ✓ {clean} (libretro, {kb} KB)")
        return "ok"
    # 2. Try archive.org thumbnail (for unknown/obscure games)
    # (Libretro is already tried first above)
    ident = archive_identifier_from_url(clean)
    if ident:
        if download_archive_thumbnail(ident, dest_path):
            kb = os.path.getsize(dest_path) // 1024
            print(f"          ✓ {clean} (archive.org, {kb} KB)")
            return "ok"
    # 3. Try Wikipedia article thumbnail
    wiki_img = wikipedia_thumbnail(clean)
    if wiki_img and download_as_jpeg(wiki_img, dest_path):
        kb = os.path.getsize(dest_path) // 1024
        print(f"          ✓ {clean} (wikipedia, {kb} KB)")
        return "ok"
    # 4. Brute force: Wikimedia Commons search (base name)
    filename = wikimedia_search_image(clean)
    if filename:
        wurl = f"{WIKIMEDIA_FILE}/{urllib.parse.quote(filename)}"
        if download_as_jpeg(wurl, dest_path):
            kb = os.path.getsize(dest_path) // 1024
            print(f"          ✓ {clean} (wikimedia, {kb} KB)")
            return "ok"
    print(f"          ✗ {clean} not found")
    return "fail"


def _scrape_steam_worker(task):
    """Scrape cover art for one Steam game (called by ThreadPoolExecutor)."""
    appid, name, steam_covers_dir = task
    dest = os.path.join(steam_covers_dir, f"{appid}.jpg")
    if os.path.isfile(dest) and os.path.getsize(dest) > 1000:
        return "skip"
    if download_steam_cover(appid, dest):
        kb = os.path.getsize(dest) // 1024
        print(f"  ✓ {name} ({appid}, {kb} KB)")
        return "ok"
    print(f"  ✗ {name} ({appid}) not found")
    return "fail"


def _generate_name_variants(name, system_name=None):
    """Generate aggressive name variants for scraping."""
    variants = set()
    base = clean_filename(name)
    variants.add(base)
    # No-Intro 'The' variant
    the_variant = _no_intro_the(base)
    if the_variant:
        variants.add(the_variant)
    # Original name
    variants.add(name)
    # Lowercase
    variants.add(base.lower())
    # Remove all spaces
    variants.add(base.replace(" ", ""))
    # Replace spaces with underscores/hyphens
    variants.add(base.replace(" ", "_"))
    variants.add(base.replace(" ", "-"))
    # Remove non-alphanumeric
    variants.add(re.sub(r"[^a-zA-Z0-9]", "", base))
    # Remove region/version tags (already done by clean_filename, but just in case)
    variants.add(re.sub(r"\s*\([^)]*\)", "", name))
    # Add system name as suffix/prefix if available
    if system_name:
        for v in list(variants):
            variants.add(f"{v} {system_name}")
            variants.add(f"{system_name} {v}")
    return [v for v in variants if v]




def _scrape_db_game_worker(task):
    """Scrape cover art for one DB game entry (called by ThreadPoolExecutor)."""
    g, covers_dir, no_odyssey, no_wiki, counter, total, lock = task
    name        = g["name"]
    system_name = g["system_name"]
    local_path  = g["local_path"]
    if not name:
        return "skip"

    if local_path and os.path.isfile(local_path):
        stem = os.path.splitext(os.path.basename(local_path))[0]
    else:
        decoded = urllib.parse.unquote(name)
        stem_candidate, ext_candidate = os.path.splitext(decoded)
        _archive_exts = {".zip", ".7z", ".gz", ".bz2", ".xz", ".rar"}
        _rom_exts = set(SYSTEM_MAP.keys()) | {".odyssey"}
        if ext_candidate.lower() in _archive_exts:
            inner, inner_ext = os.path.splitext(stem_candidate)
            stem = inner if inner_ext.lower() in _rom_exts else stem_candidate
        elif ext_candidate.lower() in _rom_exts:
            stem = stem_candidate
        else:
            stem = decoded

    clean = clean_filename(stem)
    # Aggressive variant generation
    variants = _generate_name_variants(clean, system_name)
    # Add original name as fallback
    if name not in variants:
        variants.append(name)

    with lock:
        counter[0] += 1
        idx = counter[0]
    print(f"  scraping {clean} ... {idx}/{total}")

    if system_name == "Magnavox Odyssey":
        if no_odyssey:
            return "skip"
        dest_dir  = os.path.join(covers_dir, "odyssey")
        dest_path = os.path.join(dest_dir, clean + ".jpg")
        if os.path.isfile(dest_path) and os.path.getsize(dest_path) > 1000:
            return "skip"
        os.makedirs(dest_dir, exist_ok=True)
        bare = odyssey_bare_name(stem)
        if download_odyssey_cover(bare, dest_path):
            return "ok"
        return "fail"

    if system_name == "Steam":
        return "skip"

    mapping = DB_SYSTEM_MAP.get(system_name)
    if not mapping or mapping[1] is None:
        dest_dir  = os.path.join(covers_dir, "unknown")
        dest_path = os.path.join(dest_dir, clean + ".jpg")
        if os.path.isfile(dest_path) and os.path.getsize(dest_path) > 1000:
            return "skip"
        if no_wiki:
            return "fail"
        os.makedirs(dest_dir, exist_ok=True)
        # Wikipedia, then Wikimedia Commons, then archive.org fallback
        for v in variants:
            wiki_img = wikipedia_thumbnail(v)
            if wiki_img and download_as_jpeg(wiki_img, dest_path):
                return "ok"
        for v in variants:
            filename = wikimedia_search_image(v + " game")
            if filename:
                wurl = f"{WIKIMEDIA_FILE}/{urllib.parse.quote(filename)}"
                if download_as_jpeg(wurl, dest_path):
                    return "ok"
        for v in variants:
            ident = archive_identifier_from_url(v)
            if ident:
                if download_archive_thumbnail(ident, dest_path):
                    return "ok"
        return "fail"

    sys_dir, sys_long = mapping
    dest_dir  = os.path.join(covers_dir, sys_dir)
    dest_path = os.path.join(dest_dir, clean + ".jpg")
    if os.path.isfile(dest_path) and os.path.getsize(dest_path) > 1000:
        return "skip"
    os.makedirs(dest_dir, exist_ok=True)
    # 1. Try libretro boxart (main) with all variants
    for v in variants:
        url = libretro_url(sys_long, v)
        if download_as_jpeg(url, dest_path):
            return "ok"
    # 2. Try Wikipedia article thumbnail with all variants and system name
    if not no_wiki:
        for v in variants:
            wiki_img = wikipedia_thumbnail(v)
            if wiki_img and download_as_jpeg(wiki_img, dest_path):
                return "ok"
            # Try with system name
            if system_name:
                wiki_img2 = wikipedia_thumbnail(f"{v} ({system_name})")
                if wiki_img2 and download_as_jpeg(wiki_img2, dest_path):
                    return "ok"
        # 3. Brute force: Wikimedia Commons search
        suffixes = [" game", " video game", " box art", " cover art", " front cover", " scan", " cartridge", ""]
        for v in variants:
            for suffix in suffixes:
                filename = wikimedia_search_image(v + suffix)
                if filename:
                    wurl = f"{WIKIMEDIA_FILE}/{urllib.parse.quote(filename)}"
                    if download_as_jpeg(wurl, dest_path):
                        return "ok"
    # 4. Archive.org fallback for variants
    for v in variants:
        ident = archive_identifier_from_url(v)
        if ident:
            if download_archive_thumbnail(ident, dest_path):
                return "ok"
    return "fail"

# ─── Main ─────────────────────────────────────────────────────────────────────

def main():
    # Hardcode ORACLE_APP_HOST to match the shell environment if present
    # Always hardcode ORACLE_APP_HOST to 147.224.62.87
    os.environ["ORACLE_APP_HOST"] = "147.224.62.87"
    print(f"[debug] Hardcoded ORACLE_APP_HOST to 147.224.62.87")
    _load_omnicon_env()  # Ensure Oracle DB env vars are loaded automatically
    parser = argparse.ArgumentParser(description="Scrape cover art for OmniCon ROMs")
    parser.add_argument("--roms-dir",   default="Roms",         help="Path to ROMs folder")
    parser.add_argument("--covers-dir", default="saves/covers", help="Path to covers folder")
    parser.add_argument("--steam-root", default=None,
                        help="Steam root dir (auto-detected if omitted)")
    parser.add_argument("--no-steam",   action="store_true",    help="Skip Steam games")
    parser.add_argument("--no-odyssey", action="store_true",    help="Skip Odyssey games")
    parser.add_argument("--workers",    type=int,
                        default=min(64, (os.cpu_count() or 4) * 8),
                        help="Number of parallel download threads (default: cpu*8, max 64)")
    parser.add_argument("--unknown-csv", default=None,
                        help="CSV of unknown-system games (name,download_url) to scrape covers for")
    parser.add_argument("--db",         action="store_true",
                        help="Also scrape covers for games in the Oracle/MySQL DB")
    parser.add_argument("--db-only",    action="store_true",
                        help="Only scrape DB entries (skip local ROM scan)")
    parser.add_argument("--no-wiki",    action="store_true",
                        help="Skip Wikipedia fallback (faster, libretro-only)")
    parser.add_argument("--convert-existing", action="store_true",
                        help="Convert any already-downloaded .png covers to .jpg and exit")
    args = parser.parse_args()

    roms_dir   = args.roms_dir
    covers_dir = args.covers_dir

    # Convert-existing mode: convert all PNGs in place and exit
    if args.convert_existing:
        convert_existing_covers(covers_dir)
        return

    # Always convert any leftover PNGs from previous runs before downloading new ones
    convert_existing_covers(covers_dir)

    # Unknown-system mode: just scrape the CSV and exit
    if args.unknown_csv:
        if not os.path.isfile(args.unknown_csv):
            print(f"[error] CSV not found: {args.unknown_csv}")
            sys.exit(1)
        scrape_unknown_from_csv(args.unknown_csv, covers_dir)
        return

    # ── DB-only mode: skip local ROM scan ────────────────────────────────────
    if args.db_only or args.db:
        # Check for Oracle DB env var before proceeding
        oracle_host = os.environ.get("ORACLE_APP_HOST", "")
        if not oracle_host:
            print("[db] ORACLE_APP_HOST not set — skipping DB scrape.")
            if args.db_only:
                return
        else:
            db_games = collect_db_games()
            if db_games:
                print(f"[DB] Loaded {len(db_games)} game(s) from Oracle.")
                # Pre-filter: only dispatch workers for games that are missing covers
                pending = []
                for g in db_games:
                    if args.no_odyssey and g.get("system_name") == "Magnavox Odyssey":
                        continue
                    dest = _db_game_dest_path(g, covers_dir)
                    if not dest or _cover_cached(dest):
                        continue
                    pending.append(g)
                print(f"[covers] {len(pending)} game(s) missing cover art — fetching now...")
                scrape_db_covers(db_games, covers_dir, no_odyssey=args.no_odyssey,
                                 no_wiki=args.no_wiki, max_workers=args.workers)
            else:
                print("[db] No DB games to scrape.")
            if args.db_only:
                return

    if not os.path.isdir(roms_dir):
        print(f"[error] ROMs directory not found: {roms_dir}")
        sys.exit(1)

    roms = list(collect_roms(roms_dir))
    if not roms:
        print("[info] No ROMs found.")
        return

    ok = skip = fail = 0

    # ── Pre-filter ROMs that already have covers ─────────────────────────────
    pending_roms = []
    for path, ext, stem in roms:
        clean   = clean_filename(stem)
        sys_dir = "odyssey" if ext == ".odyssey" else (SYSTEM_MAP.get(ext, (None,))[0])
        if sys_dir and _cover_cached(os.path.join(covers_dir, sys_dir, clean + ".jpg")):
            skip += 1
        else:
            pending_roms.append((path, ext, stem))

    if not pending_roms:
        print(f"All {len(roms)} ROM cover(s) already cached.\n")
    else:
        print(f"Found {len(roms)} ROM(s), {len(pending_roms)} missing cover(s). Fetching...\n")
        # ── libretro CDN (ROM files) ──────────────────────────────
        counter = [0]
        lock    = threading.Lock()
        tasks   = [
            (path, ext, stem, covers_dir, args.no_odyssey, args.no_wiki, counter, len(pending_roms), lock)
            for path, ext, stem in pending_roms
        ]
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
            results = list(pool.map(_scrape_rom_worker, tasks))
        ok   += results.count("ok")
        skip += results.count("skip")
        fail += results.count("fail")

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
                steam_covers_dir = os.path.join(covers_dir, "steam")
                os.makedirs(steam_covers_dir, exist_ok=True)
                steam_pending = [
                    (appid, name) for appid, name in steam_games
                    if not _cover_cached(os.path.join(steam_covers_dir, f"{appid}.jpg"))
                ]
                skip += len(steam_games) - len(steam_pending)
                if not steam_pending:
                    print(f"[steam] All {len(steam_games)} Steam cover(s) already cached.")
                else:
                    print(f"[steam] Found {len(steam_games)} installed game(s), "
                          f"{len(steam_pending)} missing cover(s).\n")
                    steam_tasks = [(appid, name, steam_covers_dir) for appid, name in steam_pending]
                    with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
                        steam_res = list(pool.map(_scrape_steam_worker, steam_tasks))
                    ok   += steam_res.count("ok")
                    skip += steam_res.count("skip")
                    fail += steam_res.count("fail")

    print(f"\nDone.  ✓ {ok} downloaded  •  {skip} already cached  •  {fail} not found")
    if fail > 0:
        print("(Missing covers will show as coloured placeholder tiles in the shell.)")


if __name__ == "__main__":
    main()
