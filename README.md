# OmniCon-mk-II
Private repo

## Dependencies

OmniCon bootstraps itself on a new device. `make` (via the `deps` target) runs
`scripts/install_deps.sh --build`, which installs whatever is missing: the
compiler and SDL2/MySQL/mGBA development packages via apt, RGBDS and `cppp`
for SameBoy, and the vendored SameBoy sources themselves (cloned at the commit
pinned by this repository) including the boot ROMs and `libsameboy.a`.

At startup the program also checks for its runtime dependencies (`curl`, the
`mysql` client, `steam`, SteamVR, `retroarch`, `cemu`, `Ryubing`, `Vita3K`, and
`xemu`) and automatically downloads any that are missing by running
`scripts/install_deps.sh --runtime`. Emulators are installed as AppImages in
`~/.local/bin`, so no root is required for them; apt packages use `sudo` when
needed. Steam is used from a Windows install on a WSL mount (`/mnt/<drive>`) if
one exists; otherwise the Linux client is installed from Valve's official `.deb`
(enables i386 multiarch, needs `sudo`). SteamVR is then queued for install
through whichever Steam client is in use (`steam://install/250820`), so you may
need to log in to Steam and confirm the install. Set `OMNICON_SKIP_DEP_CHECK=1` to disable the startup check, or run
`scripts/install_deps.sh` manually at any time — it is idempotent and only
installs what is missing. `xenia` (Xbox 360) is Windows-only and must be
installed manually.

## Game Boy boot ROMs

The native Game Boy emulator uses boot ROM binaries generated from the vendored
SameBoy sources. The project build generates them automatically; this requires
RGBDS (`rgbasm`, `rgblink`, and `rgbgfx`), which `make deps` installs if it is
missing. You can also build the boot ROMs directly with `make bootroms`.

## Archive.org download cookies

The ROM downloader supports a Netscape-format cookie jar for Archive.org
downloads. Export cookies for `archive.org` from a browser into
`saves/archive_cookies.txt`, then launch OmniCon normally. The cookie file is
ignored by Git.

Export all cookies for the Archive.org page, not just a single preference or
donation cookie. The jar must include the browser's active Archive.org
session/authentication cookie; otherwise restricted downloads return HTTP 401.
OmniCon keeps refreshed cookies in the separate ignored file
`saves/archive_cookies.runtime.txt` and never rewrites the exported source jar.

To use a different cookie-jar location, set `OMNICON_COOKIE_FILE` before
starting the program:

```sh
export OMNICON_COOKIE_FILE=/path/to/archive-cookies.txt
```

## PC and Dreamcast games

Place Windows PC games under `Roms/pc` or use `.exe` files in `Roms/`.
Executables are shown only in the PC Games category and launch with `wine` by
default. Set `OMNICON_PC_LAUNCHER` to use another executable launcher.

`.bin` files are not treated as PC games by extension alone. A matching `.cue`
descriptor is used to distinguish PC `MODE1` media from PlayStation `MODE2`
media; raw sector images without a descriptor fall back to PS1 detection.
Files placed under `Roms/pc` are treated as PC content. Set
`OMNICON_BIN_LAUNCHER` when the default desktop opener is not the appropriate
emulator. If the PC BIN launcher cannot start, OmniCon tries the image with the
PS1 backend as a fallback; a launcher that starts but later rejects a disc
cannot be detected automatically.

For database downloads classified as PC Games, OmniCon selects `.bin` or
`.exe` content from the archive and automatically writes a missing `MODE1/2352`
cue descriptor beside extracted BIN images. Existing cached `pc_redump` BIN
downloads are repaired when the download cache is loaded.

## BIOS files

If a `Misc BIOS/` folder exists in the repository root, the PS1 and PS2
backends use it as their libretro system directory. Extract compressed BIOS
archives into that folder first; libretro cores cannot read BIOS files directly
from a `.7z` archive. If the folder is absent, the backends use
`third_party/libretro/system/`.

Dreamcast files can be placed under `Roms/dreamcast`. OmniCon launches them
through RetroArch and the Flycast libretro core:

```sh
export OMNICON_FLYCAST_CORE=/path/to/flycast_libretro.so
```

The default core path is `third_party/libretro/flycast_libretro.so`. The
launcher appends `retroarch_flycast.cfg`, which points RetroArch at `Misc BIOS`
and uses borderless windowed fullscreen so Dreamcast games fit the display.
The Dreamcast child process also prefers the system WSL graphics libraries to
avoid an NVIDIA/Mesa Wayland symbol mismatch seen with some AppImage setups.
Use a current RetroArch frontend; older versions may crash while loading CHD
media even when the CHD and firmware are valid. Set
`OMNICON_DREAMCAST_LAUNCHER` if RetroArch is installed under another command.
Dreamcast archive downloads should contain a `.gdi`, `.cdi`, or `.chd` entry
and any required track files.

## Cover art for database games

`scrape-db-covers [max-covers] [dry-run]` (at the `omnicon>` prompt) fetches
box art for every Oracle DB game that has no cover yet and saves it to
`saves/covers/<system>/<game name>.jpg`, where the shell looks for it. Images
come from the libretro thumbnail library (no account or API key) and are
deliberately small and low quality: at most 250 px on the longest side, JPEG
quality 30 (about 7 KB each, so a full run is a few hundred MB). Only games in
the Oracle DB are scraped, and games that already have a cover are skipped, so
the command can be stopped and re-run. Use `dry-run` to see how many games would
match, or a number to download only that many.

Open `shell` afterwards to see the covers, or press **F6** in an open shell to
load covers downloaded in another terminal. Games the source has no art for
(Japan-only releases, DLC, Nintendo Switch and other systems it does not cover)
keep the coloured placeholder; their names are listed in
`saves/db_covers_unmatched.tsv`. The script needs Python 3 and Pillow
(`pip install pillow`). `--quality`, `--max-size` and `--allow-title-screens`
can be passed when running `scripts/scrape_db_covers.py` directly.

## Steam library

OmniCon lists installed Steam games, owned games from the Steam Web API
(`~/.omnicon/steam.conf`), and the **non-Steam games** you added to Steam
(*Add a Non-Steam Game*). Non-Steam games are read from
`userdata/<id>/config/shortcuts.vdf` in your Steam folder (Linux paths,
`STEAM_ROOT`, or a Windows install under `/mnt/<drive>/Program Files (x86)/Steam`
in WSL), use any custom artwork from `config/grid/`, and launch through Steam
like the rest. Steam must be installed on the machine OmniCon runs on.

Steam and Xbox tiles show a progress bar and percentage of achievements
earned (gold at 100%). Steam percentages come from the Web API
(`GetPlayerAchievements`); your Steam profile's **Game details** must be set to
Public or Steam refuses the request (OmniCon prints a warning). They are cached
in `saves/steam_achievements.tsv`. Xbox percentages come from your Xbox Live
title history (achievements earned, or gamerscore progress for newer titles), so
only titles you have launched show a bar. Games without achievements show no bar.

## Additional console families

The library also recognizes Sega Master System, Game Gear, Saturn; Nintendo
Wii, Wii U, and Switch; PlayStation Portable and Vita; Xbox and Xbox 360; Atari
7800, Lynx, and Jaguar; TurboGrafx-16 / PC Engine; ColecoVision; and
Intellivision. Put games in the matching `Roms/` subfolder (for example
`Roms/saturn`, `Roms/psp`, or `Roms/xbox360`) when the file extension is
ambiguous, such as `.iso`, `.chd`, or `.pkg`.

Each launcher-backed console has its own `*ConsoleLauncher.cpp` implementation.
`GameShell` only selects the appropriate console launcher; shared RetroArch and
external-process handling lives in `ConsoleLaunchers.cpp`.

RetroArch systems use an installed core or a core placed in
`third_party/libretro/`. Set `OMNICON_RETROARCH_LAUNCHER` if the RetroArch
executable is not named `retroarch`. Per-system core paths can be overridden:

| System | Core environment variable | Default core filename |
| --- | --- | --- |
| Master System | `OMNICON_SMS_CORE` | `genesis_plus_gx_libretro.so` |
| Game Gear | `OMNICON_GAMEGEAR_CORE` | `genesis_plus_gx_libretro.so` |
| Saturn | `OMNICON_SATURN_CORE` | `mednafen_saturn_libretro.so` |
| Wii | `OMNICON_WII_CORE` | `dolphin_libretro.so` |
| PSP | `OMNICON_PSP_CORE` | `ppsspp_libretro.so` |
| Atari 7800 | `OMNICON_ATARI7800_CORE` | `prosystem_libretro.so` |
| Atari Lynx | `OMNICON_LYNX_CORE` | `handy_libretro.so` |
| Atari Jaguar | `OMNICON_JAGUAR_CORE` | `virtualjaguar_libretro.so` |
| TurboGrafx-16 / PC Engine | `OMNICON_PCENGINE_CORE` | `mednafen_pce_fast_libretro.so` |
| ColecoVision | `OMNICON_COLECO_CORE` | `bluemsx_libretro.so` |
| Intellivision | `OMNICON_INTELLIVISION_CORE` | `freeintv_libretro.so` |

Wii U, Switch, Vita, Xbox, and Xbox 360 launch through external emulators:
`cemu` (`OMNICON_WIIU_LAUNCHER`), `Ryubing` (`OMNICON_SWITCH_LAUNCHER`),
`Vita3K` (`OMNICON_VITA_LAUNCHER`), `xemu` (`OMNICON_XBOX_LAUNCHER`), and
`xenia` (`OMNICON_XBOX360_LAUNCHER`) respectively. Install and configure those
emulators, firmware, and keys as required by their projects. For Switch,
download Ryubing (the community Ryujinx fork) from
https://git.ryujinx.app/ryubing/ryujinx/releases and put the `Ryubing`
executable on your `PATH` (or point `OMNICON_SWITCH_LAUNCHER` at it); it also
needs your console's `prod.keys` and firmware installed via its own setup. Xbox One and Xbox
Series have no emulator backend; their games come from Xbox Cloud Gaming instead
(see below).

### Xbox One and Xbox Series X|S (Xbox Cloud Gaming)

OmniCon can list the games available to your Microsoft account on Xbox Cloud
Gaming (xCloud) and open them in your browser. Games appear under the **XONE**
(Xbox One) and **XSX** (Xbox Series X|S) filters, with box art.

1. Open the **PC/Xbox** menu in the shell header and pick **Xbox One** or
   **Xbox Series X|S** (also the `XONE` / `XSX` filters). While you are not
   signed in, the list shows a sign-in prompt: press Enter / A (or click) and
   the shell shows a short code and a `microsoft.com/link` address. Enter the
   code on any phone or PC and sign in; the library loads when you finish.
   Esc / B cancels. You can also run `xbox-login` at the OmniCon prompt
   (`xbox-logout` removes the sign-in). The sign-in is stored in
   `~/.omnicon/xbox_cloud_tokens.json` (owner-only permissions). If a signed-in
   list is empty, Enter / A refreshes it and S / Y signs in again.
2. The library is cached in `saves/xbox_cloud_library.json` and refreshed after
   12 hours; if Xbox is unreachable the cached list is used.
3. Selecting a game opens `https://www.xbox.com/play/launch/game/<product id>`
   with `cmd.exe /c start` under WSL or `xdg-open` elsewhere. Xbox Cloud Gaming
   needs a browser that xbox.com supports and, for most games, Game Pass
   Ultimate. Your Microsoft account needs an Xbox profile (gamertag).

No app registration is required. Optional settings go in
`~/.omnicon/xbox.conf` (one `key=value` per line, `#` for comments); the
matching environment variable, when set, wins:

| Key | Environment variable | Meaning |
| --- | --- | --- |
| `scope` | `OMNICON_XBOX_CLOUD_SCOPE` | `mine` (default) or `all` cloud-playable titles (your history games are always included) |
| `launcher` | `OMNICON_XBOX_CLOUD_LAUNCHER` | Browser command, for example `microsoft-edge` |
| `client_id` | `OMNICON_XBOX_CLIENT_ID` | Azure client id for sign-in (default: xbox.com's) |

By default the list combines two sources: cloud-streamable titles your account
can play (owned games and, with Game Pass Ultimate, Game Pass titles), and games
from your Xbox Live game history. History games that cannot be streamed (many
paid games are not cloud-enabled) open their Microsoft Store page instead of
cloud play. Only games you have launched at least once appear in the history,
so a purchase you never started will not show up until you do. Each list ends
with a **+** tile that opens Xbox Cloud Gaming (`xbox.com/play`) so you can add
more games; press F5 (X on a controller) while on an Xbox list, or Enter / A on
an empty list, to refresh and pick them up. A game is shown under Xbox Series X|S only when the
Store lists it as Series-only; everything else is shown under Xbox One.
`scripts/xcloud_library.py` implements the sign-in and library lookup using only
the Python standard library. The xCloud endpoints are undocumented and may
change without notice.

### Wii U archives (Cemu)

Wii U downloads from Archive.org are NUS dumps (`title.tmd`, `title.tik`, and
encrypted `.app` files in a `.7z`). Cemu cannot start those directly, so
OmniCon keeps the archive in `Roms/wiiu/` and converts it on first launch:

1. `7z` unpacks the archive to a temporary folder next to it.
2. The bundled `third_party/cdecrypt` (built automatically on first use, or
   with `make cdecrypt`) decrypts it into Cemu's mlc folder as an installed
   title (`usr/title/<type>/<id>`). No keys file is needed.
3. The install is recorded in `saves/wiiu_converted.tsv`; later launches skip
   straight to Cemu.

To convert everything up front, run `convert-wiiu` (optionally with a folder,
default `Roms/wiiu`) at the OmniCon prompt. Update and DLC archives install
alongside their base game, but only the base game can be launched. Conversion
needs temporary space of about two to three times the archive size, and the
original archive is kept. Cemu's mlc folder defaults to
`~/.local/share/Cemu/mlc01`; set `OMNICON_CEMU_MLC` to use another one.

For original Xbox games, install and configure Xemu (including its required
MCPX ROM, flash ROM, EEPROM, and hard-disk image). If `xemu` is not on the
launcher process `PATH`, set `OMNICON_XBOX_LAUNCHER` to its executable. From
WSL, a Windows installation can be configured with a path such as
`/mnt/c/Program Files/xemu/xemu.exe`. OmniCon also automatically detects an
Xemu binary installed at `$HOME/.local/bin/xemu`.
