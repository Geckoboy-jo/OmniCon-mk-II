#!/bin/sh
# Downloads the libretro cores used by the extra consoles, and checks for MAME.
cd "$(dirname "$0")/.." || exit 1
DIR=third_party/libretro
mkdir -p "$DIR"
BASE=https://buildbot.libretro.com/nightly/linux/x86_64/latest
CORES="mame same_cdi o2em hatari dosbox_pure vice_x64 fuse theodore gearcoleco cap32 81 np2kai swanstation bk cannonball mrboom mednafen_vb pokemini gw mednafen_pcfx mednafen_supergrafx mednafen_pce mednafen_ngp mednafen_wswan picodrive opera freechaf vecx potator puae fbneo supermodel geolith neocd"
for c in $CORES; do
  if [ -f "$DIR/${c}_libretro.so" ]; then continue; fi
  echo "Fetching $c"
  if curl -fsSL "$BASE/${c}_libretro.so.zip" -o "/tmp/$c.zip"; then
    unzip -oq "/tmp/$c.zip" -d "$DIR" && rm -f "/tmp/$c.zip"
  else
    echo "  (unavailable: $c)"
  fi
done
if [ -x third_party/mame/mame ] || command -v mame >/dev/null 2>&1; then
  echo "MAME found."
else
  echo "MAME not found: install it (apt install mame) or set OMNICON_MAME."
fi
