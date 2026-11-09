# OmniCon toys-to-life bridge

A bridge between OmniCon and a separate simulation app for toys-to-life
hardware, so games that need physical toys are playable with figure dumps:

| Toy system | Hardware simulated | Figure format |
| --- | --- | --- |
| Skylanders | Portal of Power (32-byte HID report protocol) | 0x400-byte MIFARE Classic 1K dump (`.bin`/`.sky`/`.dmp`) |
| Disney Infinity | Infinity Base (hexagon + 2 round pads) | raw tag pages (`.bin`/`.fig`) |
| amiibo | NTAG215 NFC tag (540 bytes, page-level reads/writes) | 540-byte `.bin`/`.nfc` dump |

The app manages a figure library, validates dumps, tracks which figures are
"on" the portal/base, emulates the portal command set (status, activate,
block read/write, LED color, hot-swap change counter), and persists
game-written figure data (level progress) back to the dump files.

**Legal note:** the app manages dumps of figures you own. It ships no figure
data, no amiibo keys, and no console-unique material.

## Running the app

```sh
python3 toys_bridge/bridge_app.py            # interactive terminal UI
python3 toys_bridge/bridge_app.py --headless # TCP server only
```

Interactive commands: `list`, `place <kind> <figure> [slot]`,
`remove <kind> <slot>`, `color <r> <g> <b>`, `state`, `stage <system>`,
`save`, `rescan`, `quit`.

Put figure dumps in `toys_bridge/figures/skylanders/`, `.../infinity/`, or
`.../amiibo/`. Dumps dropped loose into `toys_bridge/figures/nfc/` (the
general character-dump folder) are auto-classified by content: 540 bytes ->
amiibo, 0x400 bytes -> Skylanders, anything else in the token range ->
Disney Infinity. Optional `figures/<kind>_names.csv` files map ids to
display names (format documented in the shipped CSVs).

## How OmniCon uses it

When you launch a title whose filename matches a toys-to-life game
(Skylanders / Disney Infinity on Wii, PS3, Wii U, Xbox 360, Switch), OmniCon
starts the app automatically, announces the game, and copies the figures
currently on the simulated hardware into `toys_bridge/active/<system>/`.
The emulator process inherits `OMNICON_TTL_FIGURES_DIR` pointing at that
folder. Launching ordinary games never spawns the app; amiibo staging only
happens when the app is already running (start it yourself and `place` an
amiibo before launching a Wii U / Switch / 3DS game).

## Emulator setup

The bridge stages figure files; the emulator's own device emulation presents
them to the game:

- **RPCS3 (PS3 Skylanders / Disney Infinity):** Settings -> I/O -> emulated
  USB devices -> Skylanders Portal / Infinity Base. Load figures from
  `toys_bridge/active/ps3/` when the game asks for a toy.
- **Dolphin standalone (Wii Skylanders):** enable the emulated Skylanders
  portal (Controllers -> emulated USB devices) and load figures from
  `toys_bridge/active/wii/`. The libretro Dolphin core has no portal
  emulation; use standalone Dolphin for Skylanders Wii titles.
- **Cemu / Citra / Ryubing (amiibo):** load the staged 540-byte `.bin` from
  `toys_bridge/active/<system>/` through the emulator's amiibo/NFC menu.

## TCP protocol

JSON-lines on `127.0.0.1:8399` (override with `OMNICON_TTL_PORT`). One
request object per line, one response object per line. Every response has
`"ok": true|false`.

```json
{"cmd":"hello"}                                   -> {"ok":true,"app":"omnicon-toys-bridge","version":1}
{"cmd":"game","system":"wii","title":"..."}       announce a launching game
{"cmd":"list","kind":"skylanders"}                -> figure library
{"cmd":"state"}                                   -> full device state
{"cmd":"place","kind":"amiibo","figure":"link"}   name, id, uid, or path
{"cmd":"remove","kind":"skylanders","slot":0}
{"cmd":"color","r":255,"g":120,"b":0}             portal LED
{"cmd":"stage","system":"wii"}                    -> {"ok":true,"dir":...,"figures":[...]}
{"cmd":"save"}                                    persist game-written figures
{"cmd":"portal_command","data":"<hex>"}           raw Portal of Power frame
                                                  (32 bytes) -> {"data":"<hex>"}
{"cmd":"quit"}
```

`portal_command` exposes the emulated Portal of Power wire protocol directly
(`S` status, `A` activate, `Q`/`W` block read/write, `C` color, `R` reset),
so any app that speaks portal HID can drive the simulation through the
bridge.

## Tests

```sh
cd toys_bridge && python3 -m unittest -v
```
