# OmniCon-mk-II
Private repo

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
