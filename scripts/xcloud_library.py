#!/usr/bin/env python3
"""Xbox Cloud Gaming library helper for OmniCon (stdlib only).

    xcloud_library.py login                  sign in with a Microsoft device code
    xcloud_library.py logout                 forget the saved sign-in
    xcloud_library.py list --cache F --covers-dir D [--all] [--refresh]

`list` prints one tab-separated line per game on stdout:
    <productId> <XboxOne|XboxSeriesX> <name> <cover path or empty> <cloud|store>
"cloud" games can be streamed; "store" games are owned but only openable on
their Store page. Everything else (prompts, progress, errors) goes to stderr.
Exit status 2 means "not signed in" (silent unless a saved sign-in expired).

The xCloud endpoints are undocumented and may change without notice.
"""
import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

def read_conf():
    """Key=value settings from ~/.omnicon/xbox.conf (environment variables win)."""
    conf = {}
    try:
        for line in (Path.home() / ".omnicon" / "xbox.conf").read_text().splitlines():
            line = line.strip()
            if line and not line.startswith("#") and "=" in line:
                key, _, value = line.partition("=")
                conf[key.strip().lower()] = value.strip()
    except OSError:
        pass
    return conf


CONF = read_conf()

# xbox.com's own web client, the same one other open-source xCloud clients
# use. Set client_id in xbox.conf (or OMNICON_XBOX_CLIENT_ID) to override it.
CLIENT_ID = (os.environ.get("OMNICON_XBOX_CLIENT_ID") or CONF.get("client_id")
             or "1f907974-e22b-4810-a9de-d9647380c97e")
SCOPE = "xboxlive.signin openid profile offline_access"
MSA_BASE = "https://login.microsoftonline.com/consumers/oauth2/v2.0"
TOKEN_FILE = Path(os.environ.get("OMNICON_XBOX_TOKENS")
                  or Path.home() / ".omnicon" / "xbox_cloud_tokens.json")
UA = "Mozilla/5.0 OmniCon-xcloud-library"
XBL_HEADERS = {"x-xbl-contract-version": "1", "Origin": "https://www.xbox.com",
               "Referer": "https://www.xbox.com/"}
OFFERINGS = ("xgpuweb", "xgpuwebf2p")  # Game Pass, then free-to-play
CATALOG_URL = "https://displaycatalog.mp.microsoft.com/v7.0/products"
CATALOG_BATCH = 50
CACHE_VERSION = 2

XERR_HELP = {
    2148916233: "this Microsoft account has no Xbox profile; sign in at xbox.com once first",
    2148916235: "Xbox Live is not available in this account's country/region",
    2148916238: "child accounts must be added to a Microsoft family before signing in",
}


class NotSignedIn(Exception):
    pass


def log(*args):
    print(*args, file=sys.stderr, flush=True)


def request(method, url, headers=None, body=None, form=None, timeout=30):
    hdrs = {"User-Agent": UA, "Accept": "application/json"}
    hdrs.update(headers or {})
    data = None
    if form is not None:
        data = urllib.parse.urlencode(form).encode()
        hdrs["Content-Type"] = "application/x-www-form-urlencoded"
    elif body is not None:
        data = json.dumps(body).encode()
        hdrs["Content-Type"] = "application/json"
    req = urllib.request.Request(url, data=data, headers=hdrs, method=method)
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        raw = resp.read()
    return json.loads(raw) if raw else {}


def http_error_body(err):
    try:
        return json.loads(err.read() or b"{}")
    except ValueError:
        return {}


# ---------------------------------------------------------------- tokens

def load_tokens():
    try:
        return json.loads(TOKEN_FILE.read_text())
    except (OSError, ValueError):
        return {}


def save_tokens(tokens):
    TOKEN_FILE.parent.mkdir(parents=True, exist_ok=True)
    fd = os.open(TOKEN_FILE, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w") as f:
        json.dump(tokens, f)


def login(machine=False):
    """Device-code sign-in. With machine=True, stdout gets line-based status for
    the OmniCon shell: 'CODE <user_code> <url>', then 'OK' or 'FAIL <reason>'."""
    def fail(message):
        if machine:
            print("FAIL " + message, flush=True)
            raise SystemExit(1)
        raise SystemExit(message)

    try:
        dc = request("POST", f"{MSA_BASE}/devicecode",
                     form={"client_id": CLIENT_ID, "scope": SCOPE})
    except (urllib.error.URLError, OSError) as e:
        fail(f"Could not reach Microsoft sign-in: {e}")
    if machine:
        print(f"CODE {dc['user_code']} {dc['verification_uri']}", flush=True)
    else:
        log(f"\nOpen {dc['verification_uri']} and enter the code: {dc['user_code']}\n"
            "Waiting for you to finish signing in...")
    deadline = time.time() + dc.get("expires_in", 900)
    interval = dc.get("interval", 5)
    while time.time() < deadline:
        time.sleep(interval)
        try:
            tok = request("POST", f"{MSA_BASE}/token", form={
                "grant_type": "urn:ietf:params:oauth:grant-type:device_code",
                "client_id": CLIENT_ID, "device_code": dc["device_code"]})
        except urllib.error.HTTPError as e:
            err = http_error_body(e).get("error", "")
            if err == "authorization_pending":
                continue
            if err == "slow_down":
                interval += 5
                continue
            fail(f"Sign-in failed: {err or e}")
        except (urllib.error.URLError, OSError):
            continue
        tok["obtained_at"] = int(time.time())
        save_tokens({"msa": tok})
        if machine:
            print("OK", flush=True)
        else:
            log("Signed in to Xbox Cloud Gaming.")
        return
    fail("The sign-in code expired. Try again.")


def msa_access_token():
    msa = load_tokens().get("msa")
    if not msa or not msa.get("refresh_token"):
        raise NotSignedIn()
    if time.time() < msa["obtained_at"] + msa.get("expires_in", 0) - 60:
        return msa["access_token"]
    try:
        tok = request("POST", f"{MSA_BASE}/token", form={
            "grant_type": "refresh_token", "client_id": CLIENT_ID,
            "refresh_token": msa["refresh_token"], "scope": SCOPE})
    except urllib.error.HTTPError as e:
        if e.code in (400, 401):
            raise NotSignedIn()
        raise
    tok["obtained_at"] = int(time.time())
    save_tokens({"msa": tok})
    return tok["access_token"]


# ---------------------------------------------------------------- Xbox Live / xCloud

def xsts_authorize(relying_party, full=False):
    """XSTS token for a relying party; full=True returns the whole response."""
    user = request("POST", "https://user.auth.xboxlive.com/user/authenticate",
                   headers=XBL_HEADERS,
                   body={"Properties": {"AuthMethod": "RPS", "SiteName": "user.auth.xboxlive.com",
                                        "RpsTicket": "d=" + msa_access_token()},
                         "RelyingParty": "http://auth.xboxlive.com", "TokenType": "JWT"})
    try:
        r = request("POST", "https://xsts.auth.xboxlive.com/xsts/authorize",
                    headers=XBL_HEADERS,
                    body={"Properties": {"SandboxId": "RETAIL", "UserTokens": [user["Token"]]},
                          "RelyingParty": relying_party, "TokenType": "JWT"})
    except urllib.error.HTTPError as e:
        xerr = http_error_body(e).get("XErr")
        raise SystemExit("Xbox sign-in refused: " + XERR_HELP.get(xerr, f"XErr {xerr}"))
    return r if full else r["Token"]


class CloudUnavailable(Exception):
    pass


def xcloud_session():
    """Returns (gsToken, baseUri) for the first offering this account can use."""
    xsts = xsts_authorize("http://gssv.xboxlive.com/")
    last = None
    for offering in OFFERINGS:
        try:
            r = request("POST", f"https://{offering}.gssv-play-prod.xboxlive.com/v2/login/user",
                        headers={"x-gssv-client": "XboxComBrowser"},
                        body={"token": xsts, "offeringId": offering})
        except urllib.error.HTTPError as e:
            last = e
            continue
        regions = r["offeringSettings"]["regions"]
        region = next((x for x in regions if x.get("isDefault")), regions[0])
        return r["gsToken"], region["baseUri"].rstrip("/")
    raise CloudUnavailable(f"This account cannot use Xbox Cloud Gaming ({last}).")


def fetch_cloud_titles():
    """Every cloud-streamable title: product id -> {name, entitled}."""
    token, base = xcloud_session()
    results = request("GET", base + "/v2/titles",
                      headers={"Authorization": "Bearer " + token})["results"]
    titles = {}
    for t in results:
        details = t.get("details") or {}
        pid = (details.get("productId") or "").upper()
        if pid.isalnum():
            titles[pid] = {"name": details.get("name") or pid,
                           "entitled": bool(details.get("hasEntitlement"))}
    return titles


def fetch_owned_titles():
    """Console games on the account's Xbox Live title history, keyed by Store
    product id: {name, generations, cover}. Covers games bought outside Game Pass
    that xCloud cannot stream. Only games that have been launched at least once
    appear in the history."""
    xsts = xsts_authorize("http://xboxlive.com", full=True)
    claims = xsts["DisplayClaims"]["xui"][0]
    headers = {"Authorization": f"XBL3.0 x={claims['uhs']};{xsts['Token']}",
               "x-xbl-contract-version": "2", "Accept-Language": "en-US"}
    base = f"https://titlehub.xboxlive.com/users/xuid({claims['xid']})/titles/titlehistory/decoration/"
    results = None
    for fields in ("scid,image,productId", "scid,image"):
        try:
            results = request("GET", base + fields + "?maxItems=2000", headers=headers,
                              timeout=60).get("titles", [])
            break
        except urllib.error.HTTPError as e:
            log(f"title history ({fields}) failed: HTTP {e.code}")
    if results is None:
        return {}

    owned, skipped = {}, 0
    for t in results:
        devices = t.get("devices") or []
        console = [d for d in devices if d == "XboxOne" or d.startswith("XboxSeries")]
        if not console or t.get("type", "Game") != "Game":
            continue
        pid = (t.get("bingId") or t.get("productId") or "").upper()
        if len(pid) != 12 or not pid.isalnum():
            skipped += 1
            continue
        owned[pid] = {"name": t.get("name") or pid,
                      "series_only": "XboxOne" not in devices,
                      "cover": (t.get("displayImage") or "")}
    if skipped:
        log(f"{skipped} title(s) in your Xbox history have no Store id and were skipped.")
    return owned


# ---------------------------------------------------------------- catalog

def pick_cover(images):
    for purpose in ("Poster", "BoxArt", "FeaturePromotionalSquareArt"):
        for img in images:
            if img.get("ImagePurpose") == purpose and img.get("Uri"):
                uri = img["Uri"]
                return ("https:" + uri if uri.startswith("//") else uri) + "?w=400"
    return ""


def describe(ids):
    """Product id -> {name, generation, cover} from the public display catalog."""
    out = {}
    for i in range(0, len(ids), CATALOG_BATCH):
        batch = ids[i:i + CATALOG_BATCH]
        url = (CATALOG_URL + "?" + urllib.parse.urlencode({
            "bigIds": ",".join(batch), "market": "US", "languages": "en-US",
            "MS-CV": "DGU1mcuYo0WMMp+F.1"}))
        try:
            products = request("GET", url).get("Products", [])
        except (urllib.error.URLError, ValueError) as e:
            log(f"catalog lookup failed for a batch: {e}")
            continue
        for p in products:
            loc = (p.get("LocalizedProperties") or [{}])[0]
            props = p.get("Properties") or {}
            gens = props.get("XboxConsoleGenCompatible") or props.get("XboxConsoleGenOptimized") or []
            series_only = "ConsoleGen9" in gens and "ConsoleGen8" not in gens
            out[p.get("ProductId")] = {
                "name": loc.get("ProductTitle") or "",
                "generation": "XboxSeriesX" if series_only else "XboxOne",
                "cover": pick_cover(loc.get("Images") or []),
            }
    return out


def build_library(include_all):
    failures = []
    try:
        cloud = fetch_cloud_titles()
    except (CloudUnavailable, urllib.error.URLError, OSError, KeyError, ValueError) as e:
        log(f"Cloud streaming list unavailable: {e}")
        failures.append(e)
        cloud = {}
    try:
        owned = fetch_owned_titles()
    except (urllib.error.URLError, OSError, KeyError, IndexError, ValueError) as e:
        log(f"Could not read your Xbox game history: {e}")
        failures.append(e)
        owned = {}
    if len(failures) == 2:
        # Don't replace a good cached library with an empty one.
        raise OSError("both Xbox services failed")

    wanted = {pid: v["name"] for pid, v in cloud.items() if include_all or v["entitled"]}
    for pid, v in owned.items():
        wanted.setdefault(pid, v["name"])
    info = describe(list(wanted))

    games = []
    for pid, fallback in wanted.items():
        meta = info.get(pid, {})
        own = owned.get(pid, {})
        series_only = (meta["generation"] == "XboxSeriesX" if "generation" in meta
                       else own.get("series_only", False))
        cover = meta.get("cover") or (own.get("cover") + "?w=400" if own.get("cover") else "")
        games.append({"id": pid,
                      "name": " ".join((meta.get("name") or fallback).split()),
                      "generation": "XboxSeriesX" if series_only else "XboxOne",
                      "cover": cover,
                      "kind": "cloud" if pid in cloud else "store"})
    games.sort(key=lambda g: g["name"].lower())
    return games


# ---------------------------------------------------------------- list command

def load_cache(path):
    try:
        data = json.loads(Path(path).read_text())
    except (OSError, ValueError):
        return None
    return data if data.get("version") == CACHE_VERSION else None


def save_cache(path, include_all, games):
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_text(json.dumps({"version": CACHE_VERSION, "all": include_all,
                                      "fetched_at": int(time.time()), "games": games}))


def download_cover(game, covers_dir):
    dest = Path(covers_dir) / game["generation"].lower() / (game["id"] + ".jpg")
    if dest.exists() or not game["cover"]:
        return
    dest.parent.mkdir(parents=True, exist_ok=True)
    tmp = dest.with_suffix(".part")
    try:
        req = urllib.request.Request(game["cover"], headers={"User-Agent": UA})
        with urllib.request.urlopen(req, timeout=20) as resp:
            tmp.write_bytes(resp.read())
        tmp.replace(dest)
    except (urllib.error.URLError, OSError):
        tmp.unlink(missing_ok=True)


def cmd_list(args):
    # Not signed in is the normal state for users who don't use Xbox, so stay quiet.
    if not load_tokens().get("msa"):
        return 2
    scope = os.environ.get("OMNICON_XBOX_CLOUD_SCOPE") or CONF.get("scope", "")
    include_all = args.all or scope.lower() == "all"
    cache = load_cache(args.cache)
    fresh = (cache and cache["all"] == include_all and not args.refresh
             and time.time() - cache["fetched_at"] < args.max_age_hours * 3600)
    if fresh:
        games = cache["games"]
    else:
        try:
            games = build_library(include_all)
            save_cache(args.cache, include_all, games)
        except NotSignedIn:
            log("Xbox Cloud Gaming sign-in expired. Run 'xbox-login' at the OmniCon prompt.")
            return 2
        except (urllib.error.URLError, OSError) as e:
            if not cache or cache["all"] != include_all:
                log(f"Could not reach Xbox Cloud Gaming: {e}")
                return 1
            log(f"Could not reach Xbox Cloud Gaming ({e}); using the cached library.")
            games = cache["games"]

    with ThreadPoolExecutor(max_workers=8) as pool:
        list(pool.map(lambda g: download_cover(g, args.covers_dir), games))

    for g in games:
        cover = Path(args.covers_dir) / g["generation"].lower() / (g["id"] + ".jpg")
        print("\t".join((g["id"], g["generation"], g["name"], str(cover) if cover.exists() else "",
                         g.get("kind", "cloud"))))
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    log_in = sub.add_parser("login")
    log_in.add_argument("--machine", action="store_true",
                        help="print line-based status for the OmniCon shell")
    sub.add_parser("logout")
    lst = sub.add_parser("list")
    lst.add_argument("--cache", required=True)
    lst.add_argument("--covers-dir", required=True)
    lst.add_argument("--all", action="store_true",
                     help="list every cloud-playable title, not just ones you can play")
    lst.add_argument("--refresh", action="store_true")
    lst.add_argument("--max-age-hours", type=float, default=12)
    args = parser.parse_args()
    sys.stdout.reconfigure(encoding="utf-8")

    if args.command == "login":
        login(args.machine)
        return 0
    if args.command == "logout":
        TOKEN_FILE.unlink(missing_ok=True)
        log("Signed out of Xbox Cloud Gaming.")
        return 0
    return cmd_list(args)


if __name__ == "__main__":
    sys.exit(main())
