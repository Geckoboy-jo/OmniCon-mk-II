#!/usr/bin/env python3
"""Scrape low-quality cover art for OmniCon's Oracle DB games (stdlib + Pillow).

    scrape_db_covers.py --input games.tsv --covers-dir saves/covers
                        [--limit N] [--dry-run] [--quality 30] [--max-size 250]
                        [--unmatched FILE] [--workers 8] [--allow-title-screens]

--input is a UTF-8 TSV written by OmniCon, one game per line:
    <db id> <TAB> <system dir, e.g. nes> <TAB> <display name>
Covers are saved as <covers-dir>/<system dir>/<display name>.jpg, the path the
shell already looks at, so they appear in the shell the next time covers are
resolved (restart the shell, or press F6 inside it).

Images come from the libretro thumbnail library (https://thumbnails.libretro.com),
which needs no account or API key. Each image is shrunk to --max-size pixels on
its longest side and saved as a heavily compressed JPEG. Games that already have
a cover are skipped, and the run can be repeated or interrupted safely.
"""
import argparse
import html
import io
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from collections import defaultdict
from concurrent.futures import FIRST_COMPLETED, ThreadPoolExecutor, wait
from pathlib import Path

BASE = "https://thumbnails.libretro.com/"
UA = "Mozilla/5.0 OmniCon-cover-scraper"
MAX_DOWNLOAD = 8 * 1024 * 1024
COVER_EXTS = (".jpg", ".jpeg", ".png")

# OmniCon system dir -> libretro thumbnail folder(s), tried in order.
FOLDERS = {
    "nes": ["Nintendo - Nintendo Entertainment System"],
    "snes": ["Nintendo - Super Nintendo Entertainment System"],
    "gb": ["Nintendo - Game Boy"],
    "gbc": ["Nintendo - Game Boy Color"],
    "gba": ["Nintendo - Game Boy Advance"],
    "nds": ["Nintendo - Nintendo DS", "Nintendo - Nintendo DSi"],
    "3ds": ["Nintendo - Nintendo 3DS", "Nintendo - Nintendo 3DS (Digital)"],
    "n64": ["Nintendo - Nintendo 64"],
    "gamecube": ["Nintendo - GameCube"],
    "wii": ["Nintendo - Wii"],
    "wiiu": ["Nintendo - Wii U"],
    "fds": ["Nintendo - Family Computer Disk System"],
    "virtualboy": ["Nintendo - Virtual Boy"],
    "pokemonmini": ["Nintendo - Pokemon Mini"],
    "satellaview": ["Nintendo - Satellaview"],
    "atari2600": ["Atari - 2600"],
    "atari5200": ["Atari - 5200"],
    "atari7800": ["Atari - 7800"],
    "atarilynx": ["Atari - Lynx"],
    "atarijaguar": ["Atari - Jaguar"],
    "atari800": ["Atari - 8-bit Family", "Atari - 8-bit"],
    "atarixegs": ["Atari - 8-bit Family", "Atari - 8-bit"],
    "genesis": ["Sega - Mega Drive - Genesis"],
    "mastersystem": ["Sega - Master System - Mark III"],
    "gamegear": ["Sega - Game Gear"],
    "saturn": ["Sega - Saturn"],
    "dreamcast": ["Sega - Dreamcast"],
    "segacd": ["Sega - Mega-CD - Sega CD"],
    "sega32x": ["Sega - 32X"],
    "segapico": ["Sega - PICO"],
    "sg1000": ["Sega - SG-1000"],
    "naomi": ["Sega - Naomi"],
    "naomi2": ["Sega - Naomi 2"],
    "ps1": ["Sony - PlayStation"],
    "ps2": ["Sony - PlayStation 2"],
    "ps3": ["Sony - PlayStation 3", "Sony - PlayStation 3 (Downloadable)"],
    "psp": ["Sony - PlayStation Portable"],
    "vita": ["Sony - PlayStation Vita"],
    "xbox": ["Microsoft - Xbox"],
    "xbox360": ["Microsoft - Xbox 360"],
    "pcengine": ["NEC - PC Engine - TurboGrafx 16"],
    "pcenginecd": ["NEC - PC Engine CD - TurboGrafx-CD"],
    "supergrafx": ["NEC - PC Engine SuperGrafx"],
    "pcfx": ["NEC - PC-FX"],
    "neogeoaes": ["SNK - Neo Geo"],
    "neogeomvs": ["SNK - Neo Geo"],
    "neogeocd": ["SNK - Neo Geo CD"],
    "neogeopocket": ["SNK - Neo Geo Pocket"],
    "neogeopocketcolor": ["SNK - Neo Geo Pocket Color"],
    "colecovision": ["Coleco - ColecoVision"],
    "intellivision": ["Mattel - Intellivision"],
    "3do": ["The 3DO Company - 3DO"],
    "vectrex": ["GCE - Vectrex"],
    "wonderswan": ["Bandai - WonderSwan"],
    "wonderswancolor": ["Bandai - WonderSwan Color"],
    "channelf": ["Fairchild - Channel F"],
    "supervision": ["Watara - Supervision"],
    "amigacd32": ["Commodore - CD32"],
}

REGIONS = ("USA", "World", "Europe", "Japan")
DLC_TAGS = re.compile(r"dlc|addon|add-on|update|demo|trial|unlock|avatar|theme|expansion|pack", re.I)
LIBRETRO_BAD = re.compile(r'[&*/:`<>?\\|]')


def log(*a):
    print(*a, flush=True)


def fetch(url, timeout=25):
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    for attempt in range(3):
        try:
            with urllib.request.urlopen(req, timeout=timeout) as resp:
                data = resp.read(MAX_DOWNLOAD + 1)
            if len(data) > MAX_DOWNLOAD:
                raise ValueError("image too large")
            return data
        except urllib.error.HTTPError as e:
            if e.code in (403, 404):
                return None
            if attempt == 2:
                return None
        except (urllib.error.URLError, OSError, ValueError):
            if attempt == 2:
                return None
        time.sleep(1 + attempt)
    return None


# ---------------------------------------------------------------- name matching

def tags_of(name):
    return {t.strip().lower() for t in re.findall(r"[\(\[]([^\)\]]*)[\)\]]", name)}


def title_key(name):
    """Name with (tags)/[tags] removed, articles moved to the front, alnum only."""
    s = re.sub(r"[\(\[][^\)\]]*[\)\]]", " ", name)
    s = html.unescape(s).lower().replace("&", " and ")
    m = re.match(r"^(.*?),\s*(the|a|an)\b(.*)$", s)
    if m:
        s = f"{m.group(2)} {m.group(1)}{m.group(3)}"
    return re.sub(r"[^a-z0-9]+", "", s)


def libretro_name(name):
    return LIBRETRO_BAD.sub("_", name)


class Index:
    """The set of boxart files in one libretro folder."""

    def __init__(self, folder, files):
        self.folder = folder
        self.exact = {f: f for f in files}
        self.by_key = defaultdict(list)
        for f in files:
            self.by_key[title_key(f)].append(f)

    def _pick(self, candidates, wanted_tags):
        def score(f):
            t = tags_of(f)
            region = next((i for i, r in enumerate(REGIONS) if r.lower() in t), len(REGIONS))
            return (-len(t & wanted_tags), region, len(f))
        return min(candidates, key=score)

    def match(self, name):
        name = re.sub(r"\.(xiso|iso|zip|7z|bin|cue|chd|rom)$", "", name, flags=re.I)
        wanted = tags_of(name)
        hit = self.exact.get(libretro_name(name))
        if hit:
            return hit
        cands = self.by_key.get(title_key(name))
        if cands:
            return self._pick(cands, wanted)
        # DLC, add-ons and demos borrow the cover of the game they belong to.
        if any(DLC_TAGS.search(t) for t in wanted) and " - " in name:
            base = name.split(" - ")[0]
            cands = self.by_key.get(title_key(base))
            if cands:
                return self._pick(cands, wanted)
        return None


def load_index(folder, art_type):
    url = BASE + urllib.parse.quote(folder) + "/" + art_type + "/"
    data = fetch(url, timeout=60)
    if not data:
        return None
    page = data.decode("utf-8", "replace")
    files = []
    for href in re.findall(r'href="([^"]+\.png)"', page):
        files.append(urllib.parse.unquote(href)[:-4])
    return files


# ---------------------------------------------------------------- images

def to_low_quality_jpeg(png_bytes, max_size, quality):
    from PIL import Image
    Image.MAX_IMAGE_PIXELS = 40_000_000
    with Image.open(io.BytesIO(png_bytes)) as im:
        im = im.convert("RGBA")
        background = Image.new("RGB", im.size, (255, 255, 255))
        background.paste(im, mask=im.split()[3])
        background.thumbnail((max_size, max_size))
        out = io.BytesIO()
        background.save(out, "JPEG", quality=quality, optimize=True)
        return out.getvalue()


def destination(covers_dir, system_dir, name):
    """Cover path the shell expects, or None if the name can't be a plain file name."""
    if (not name or name in (".", "..") or any(c in name for c in "/\\\0")
            or len(name.encode("utf-8")) > 200):
        return None
    return Path(covers_dir) / system_dir / (name + ".jpg")


def has_cover(covers_dir, system_dir, name):
    return any((Path(covers_dir) / system_dir / (name + e)).exists() for e in COVER_EXTS)


def download_one(url, dests, max_size, quality):
    data = fetch(url)
    if not data:
        return 0
    try:
        jpeg = to_low_quality_jpeg(data, max_size, quality)
    except Exception:
        return 0
    saved = 0
    for dest in dests:
        try:
            dest.parent.mkdir(parents=True, exist_ok=True)
            tmp = dest.with_suffix(".part")
            tmp.write_bytes(jpeg)
            tmp.replace(dest)
            saved += 1
        except OSError:
            pass
    return saved


# ---------------------------------------------------------------- main

def read_games(path):
    games = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            parts = line.rstrip("\r\n").split("\t", 2)
            if len(parts) == 3 and parts[2]:
                games.append((parts[0], parts[1], parts[2]))
    return games


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--input", required=True)
    ap.add_argument("--covers-dir", required=True)
    ap.add_argument("--limit", type=int, default=0, help="stop after this many covers (0 = no limit)")
    ap.add_argument("--dry-run", action="store_true", help="only report what would match")
    ap.add_argument("--quality", type=int, default=30, help="JPEG quality 1-95 (default 30)")
    ap.add_argument("--max-size", type=int, default=250, help="longest side in pixels (default 250)")
    ap.add_argument("--workers", type=int, default=8)
    ap.add_argument("--unmatched", help="write the names that found no cover to this file")
    ap.add_argument("--allow-title-screens", action="store_true",
                    help="use title-screen images for games with no box art")
    args = ap.parse_args()
    args.quality = max(1, min(95, args.quality))
    args.max_size = max(64, min(1024, args.max_size))
    args.workers = max(1, min(16, args.workers))

    if not args.dry_run:
        try:
            import PIL  # noqa: F401
        except ImportError:
            log("Pillow is required to shrink images. Install it with: pip install pillow")
            return 1

    games = read_games(args.input)
    todo = [g for g in games
            if destination(args.covers_dir, g[1], g[2]) and not has_cover(args.covers_dir, g[1], g[2])]
    log(f"[covers] {len(games)} game(s) in the database list, {len(todo)} without a cover.")
    if not todo:
        return 0

    art_types = ["Named_Boxarts"] + (["Named_Titles"] if args.allow_title_screens else [])
    indexes = {}  # (system dir, art type) -> [Index]
    for sysdir in sorted({g[1] for g in todo}):
        folders = FOLDERS.get(sysdir)
        if not folders:
            continue
        for art in art_types:
            for folder in folders:
                files = load_index(folder, art)
                if files:
                    indexes.setdefault((sysdir, art), []).append(Index(folder, files))
        found = sum(len(v) for (s, _), v in indexes.items() if s == sysdir)
        log(f"[covers] {sysdir}: {found} thumbnail folder(s) available")

    jobs = defaultdict(list)           # url -> [dest paths]
    matched = defaultdict(int)
    unmatched = defaultdict(list)
    for dbid, sysdir, name in todo:
        url = None
        for art in art_types:
            for idx in indexes.get((sysdir, art), []):
                hit = idx.match(name)
                if hit:
                    url = (BASE + urllib.parse.quote(idx.folder) + "/" + art + "/"
                           + urllib.parse.quote(hit + ".png"))
                    break
            if url:
                break
        if url:
            jobs[url].append(destination(args.covers_dir, sysdir, name))
            matched[sysdir] += 1
        else:
            unmatched[sysdir].append(name)

    log("[covers] matches by system (matched / without cover):")
    for sysdir in sorted({g[1] for g in todo}):
        reason = "" if sysdir in FOLDERS else "   (no thumbnail source for this system)"
        log(f"    {sysdir:20s} {matched[sysdir]:6d} / {matched[sysdir] + len(unmatched[sysdir]):6d}{reason}")
    if args.unmatched:
        with open(args.unmatched, "w", encoding="utf-8") as f:
            for sysdir, names in sorted(unmatched.items()):
                for n in names:
                    f.write(f"{sysdir}\t{n}\n")

    if args.dry_run:
        log(f"[covers] dry run: {sum(matched.values())} cover(s) could be downloaded "
            f"({len(jobs)} unique image(s)).")
        return 0

    saved = failed = 0
    remaining = args.limit if args.limit > 0 else None
    items = list(jobs.items())
    log(f"[covers] downloading {len(items)} image(s) at {args.max_size}px, JPEG quality {args.quality}...")
    try:
        with ThreadPoolExecutor(max_workers=args.workers) as pool:
            futures = {}
            it = iter(items)
            done_count = 0

            def submit_next():
                nonlocal remaining
                if remaining is not None and remaining <= 0:
                    return False
                try:
                    url, dests = next(it)
                except StopIteration:
                    return False
                if remaining is not None:
                    dests = dests[:remaining]
                    remaining -= len(dests)
                futures[pool.submit(download_one, url, dests, args.max_size, args.quality)] = url
                return True

            for _ in range(args.workers * 2):
                if not submit_next():
                    break
            while futures:
                finished, _ = wait(list(futures), return_when=FIRST_COMPLETED)
                for fut in finished:
                    futures.pop(fut)
                    n = fut.result()
                    saved += n
                    failed += 0 if n else 1
                    done_count += 1
                    if done_count % 50 == 0:
                        log(f"[covers] {done_count}/{len(items)} processed, {saved} saved")
                    submit_next()
    except KeyboardInterrupt:
        log("[covers] interrupted; covers saved so far are kept.")
    log(f"[covers] done: {saved} cover(s) saved, {failed} download(s) failed, "
        f"{sum(len(v) for v in unmatched.values())} game(s) had no match.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
