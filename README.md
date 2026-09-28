# nornscope

Remote screen + control for the [monome norns](https://monome.org/docs/norns/).
Watch the norns screen live in a desktop window and operate its three keys and
three encoders with the mouse — works with **any** script, no script-side
support needed.

![what it looks like](docs/screenshot.png)

## How it works

- **Video out:** the norns streams its 128×64 screen over the network via
  [ndi-mod](https://github.com/Dewb/ndi-mod) (NDI protocol). nornscope receives
  it with the official NDI SDK and renders it, integer-scaled.
- **Control in:** key and encoder events are injected through matron's
  websocket REPL (`ws://<norns>:5555`, the same channel the maiden editor
  uses) by evaluating `_norns.key(n,z)` / `_norns.enc(n,d)` — the exact
  dispatch functions matron calls for physical hardware input. This makes it
  fully generic: scripts, system menus, everything responds as if you touched
  the device.

## Features

- Live screen view, pixel-perfect integer scaling, resizable window
- **K1 / K2 / K3** — click to tap; press and drag off the button to leave it
  held ("sticky"); click again to release. Combos are played the way the
  hardware encourages: stick K1 like a Shift key, then tap K2/K3.
- **E1 / E2 / E3** — hover and scroll; 3 wheel ticks per encoder detent so
  values don't fly off (`WHEEL_PER_DETENT`)
- Connection status dot (green = control channel live, red = dropped)
- Auto-reconnect of the control channel; all keys released on exit so the
  device is never left with a stuck key
- Control host auto-derived from the NDI source address — zero config

## Requirements

**Desktop (Linux; developed on Fedora):**
- g++, SDL2 (`SDL2-devel`), pkg-config
- [NDI SDK v6 for Linux](https://downloads.ndi.tv/SDK/NDI_SDK_Linux/Install_NDI_SDK_v6_Linux.tar.gz)
  (free, license-restricted — not vendored here)
- Same LAN/subnet as the norns (NDI discovery uses mDNS)

**Norns:**
- [ndi-mod](https://github.com/Dewb/ndi-mod) installed and enabled
  (SYSTEM > MODS, then SYSTEM > RESTART)
- Recommended: the menu-mode streaming patch below

## Build

```sh
# download and unpack the NDI SDK, then symlink it into the repo:
ln -s "/path/to/NDI SDK for Linux" ndi-sdk
make
```

## Norns-side setup

1. Install ndi-mod from the maiden console:
   ```
   ;install https://github.com/Dewb/ndi-mod/releases/latest/download/ndi-mod.zip
   ```
2. Enable it: SYSTEM > MODS > NDI-MOD (enc 3 to add `+`), then SYSTEM > RESTART.

### Menu-mode patch (recommended)

Stock ndi-mod only pushes a frame when the *script* redraws, so the NDI stream
freezes on the last script frame whenever you enter the system menu (K1).
`norns/mod.lua` is a drop-in replacement for
`~/dust/code/ndi-mod/lib/mod.lua` that adds a 15 Hz metro calling
`ndi_mod.update()` unconditionally, so menus stream too. It re-arms after
script reloads (norns frees all metros on script change).

## Usage

```sh
./ndi_view            # connect to first NDI source matching "NORNS"
./ndi_view MySource   # match a different source name substring
```

Helpers:

```sh
./ndi_grab                          # CLI: discover, grab one frame -> /tmp/norns_ndi.ppm
python3 tools/ws_send.py '<lua>'    # one-off commands on the matron REPL
```

`ws_send.py` examples:

```sh
python3 tools/ws_send.py '_norns.key(3,1)' '_norns.key(3,0)'   # tap K3
python3 tools/ws_send.py '_norns.enc(1,-2)'                      # E1 CCW 2
python3 tools/ws_send.py 'print(norns.menu.status())'
```

## Troubleshooting

- **Firewall:** open mDNS and the NDI port range on the desktop, e.g. firewalld:
  ```sh
  sudo firewall-cmd --permanent --add-service=mdns
  sudo firewall-cmd --permanent --add-port=5960-5989/tcp --add-port=5960-5989/udp
  sudo firewall-cmd --reload
  ```
- **OBS instead of a dedicated viewer:** OBS + DistroAV works too. On Flathub,
  DistroAV 6.x discovers sources through the host Avahi daemon and the sandbox
  blocks it by default — fix with:
  ```sh
  flatpak override --user --system-talk-name=org.freedesktop.Avahi com.obsproject.Studio
  ```
- **Flaky stream on recent norns OS:** the 60 Hz display refresh change broke
  ndi-mod for some users; norns update 2.9.4 (2026-01) shipped a screen-context
  fix for it. Keep the norns updated.

## Repo layout

```
src/ndi_view.cpp   GUI viewer + remote control (SDL2 + NDI SDK + ws REPL)
src/ndi_grab.cpp   single-frame CLI grabber / connectivity test
tools/ws_send.py   minimal stdlib websocket client for the matron REPL
norns/mod.lua      ndi-mod patch: stream system menus too
Makefile           build (set NDI_SDK=<path> if not using the ./ndi-sdk symlink)
```

## License

[MIT](LICENSE) — use it for anything, just keep the copyright notice.

## Credits

- [Dewb/ndi-mod](https://github.com/Dewb/ndi-mod) — the norns-side NDI sender
- NDI® is a trademark of Vizrt/NewTek; the NDI SDK is used under its license
- [monome norns](https://github.com/monome/norns)
