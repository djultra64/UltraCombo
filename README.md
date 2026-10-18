# Killer Instinct 2 — native port

Runs the arcade game's own code (statically recompiled to C) on Linux and
Windows, with the Midway DCS sound board and IDE hard disk emulated. You
need your own copies of the game data; none are included.

## Setup

Put these in `run/` (or point `ki2.ini` at them):

- `ki2-l14.u98` — boot ROM
- `kinst2.raw` — the hard disk, extracted from the MAME CHD:
  `chdman extractraw -i kinst2.chd -o kinst2.raw`
- `ki2_l1.u10` … `ki2_l1.u13`, `ki2_l1.u33` … `ki2_l1.u36` — sound ROMs

Then start `ki2` (`ki2.exe` on Windows). On the first launch the program
shows "PREPARING THE GAME FOR THIS PC": it boots your game files on its
interpreter until the game has loaded its code, snapshots that code,
translates it to C and compiles it into a native module. That takes about a
minute, once; the module is cached in `cache/` (a new one is made if the game
files change). The compiler used is the first that works of:

1. Zig, bundled in `tools/zig` — what the release packages use
2. a C compiler already installed (`cc`, `gcc` or `clang`)
3. TinyCC, bundled in `tools/tcc` — builds in seconds, but its code runs at
   roughly half the speed (still well above full speed on most PCs)

If none works the game runs on the interpreter, with a warning; see
`build.log` in `cache/src-*/`. All three produce code that behaves identically, so
netplay works between them.

Settings, the log, screenshots, states and the cache are written next to the
program if that folder is writable, else into the user data folder
(`%APPDATA%\KI2Port\ki2\` on Windows, `~/.local/share/KI2Port/ki2/` on
Linux), whose `run/` folder is also searched for the game files.

### Building

Needs gcc, SDL2 and python3:

    make            # the program; builds the game code at first launch
    make recomp     # developers: link the game code in instead (a minute)
    ./ki2

`make recomp` puts the generated C in `build/rec/`, derived from your ROM;
that binary contains game code and must not be shared.

### Windows

Cross-compiled from Linux with MinGW-w64 (`gcc-mingw-w64-x86-64`) and the
official SDL2 MinGW package, unpacked into `deps/sdl2-mingw/`:

    make windows    # -> dist/windows/ki2.exe + SDL2.dll

On Windows the log goes to `ki2.log` beside the executable. Linux and
Windows builds emulate identically (same state after the same inputs).

### Release packages

    make packages   # -> dist/KI2-Port-Linux.tar.xz, dist/KI2-Port-x86_64.AppImage,
                    #    dist/KI2-Port-Windows.zip

These hold the program, Zig and TinyCC, shaders and licences — no game files
and no game code. They expect, in `deps/`: Zig 0.17 for Linux and Windows
(`zig-linux/`, `zig-windows/`), TinyCC's `libtcc.so` built with that Zig
against glibc 2.31 (`tcc-lnx231/`) and `libtcc.dll` (`tcc-win/`). The Linux
program is built with Zig against glibc 2.31 too, so it runs on distributions
from about 2020 on, and so is SDL2 2.32.10 (from its source release, into
`deps/sdl2-lnx/build`; configure with `-DCMAKE_LIBRARY_ARCHITECTURE=x86_64-linux-gnu`
and `-idirafter /usr/include` so it finds the X11/Wayland/audio headers),
which ships in `lib/`.

    make package-appimage   # -> dist/KI2-Port-x86_64.AppImage

The AppImage is the Linux package plus AppImage's static runtime
(`deps/dl/appimage-runtime/runtime-x86_64`, from
github.com/AppImage/type2-runtime), packed with `mksquashfs`. Players make a
`run/` folder beside the `.AppImage` for the game files; settings and the
cache then go there too. Without one, the user data folder is used.

"Killer Instinct" is a trademark of its owners; this project is not
affiliated with them. Keep that in mind when naming anything you publish.

## Controls

| Action | Player 1 | Player 2 | Pad (Xbox names) |
|---|---|---|---|
| Move | Arrow keys | I J K L | D-pad / left stick |
| Quick / Medium / Fierce punch | A S D | T Y U | X / Y / RB |
| Quick / Medium / Fierce kick | Z X C | G H B | A / B / RT |
| Start | 1 | 2 | Start |
| Coin | 5 | 6 | Back |

The game runs in **free play** by default (the board's coinage DIP
switch): from the attract demo, the first Start goes to the title screen
and the second starts the game, as on a free-play cabinet. With free play
off, `auto_coin` makes Start insert the coins it needs. Blood level,
finishing moves and demo sounds are in `ki2.ini` too ([game]); the game
reads these switches at power-on, so changes apply after Reset.

The first pad connected is player 1, the second player 2.

| Hotkey | |
|---|---|
| Esc or F1 | Menu (pad: Guide, or Back+Start) |
| F5 / F7 | Save / load state in the current slot |
| F6 | Next state slot (1-9) |
| Backspace (hold) | Rewind |
| Tab (hold) | Fast-forward |
| P | Pause |
| . | Frame advance (pauses first) |
| F11 or Alt+Enter | Fullscreen |
| F12 | Screenshot to `screenshots/` |
| F3 | Input display |
| F4 | Training mode |
| F8 / F9 | Record / play the training dummy |
| F10 | Performance overlay |
| = / - / M | Volume up / down / mute |
| F2 | Operator test menu |
| 9 | Service credit |
| Page Up / Page Down | The cabinet's own volume buttons |

The menu has save/load, run-ahead, rewind, training, input display, video,
volume and auto-coin settings, Reset and Quit; changes are saved to
`ki2.ini` when it closes.

## Graphics

Menu → Graphics: fullscreen (borderless, or exclusive at a chosen display
mode), window size, scaling (fit 4:3, integer, stretch), shader, shader
parameters and frame pacing.

**Shaders** are drawn by the GPU. Four are built in:

- **Sharp** — crisp pixels, evenly sized at any window size
- **Smooth** — sharpened bicubic; suits KI2's pre-rendered 3D art
- **Scanlines**, **CRT** — simple, fast CRT looks

Any **libretro GLSL preset** (`.glslp`, the format of RetroArch's shader
collection) under `shaders/` works too, multi-pass presets, lookup
textures and all. Bundled: crt-lottes, crt-hyllian, crt-nobody, crt-1tap/2tap,
ScaleFX and xBR. `make shaders-extra` adds xBRZ, crt-guest-dr-venom,
crt-royale, crt-geom, crt-easymode, zfast-crt and crt-aperture (GPL
licensed, kept as separate files in `shaders/extra/`). Each preset's
`#pragma parameter` settings can be adjusted live in Shader parameters, and
are remembered. `ki2 --test-shaders` checks that every preset compiles.

Without OpenGL the game falls back to plain SDL drawing (sharp or smooth).

## Controls page

Menu → Controls rebinds both players' keys, the gamepad buttons and all
hotkeys (select one, press the new key or button; Backspace unbinds).
Also there: **left+right = neutral** (SOCD cleaning, on by default: left and
right together count as neither, up and down together as up, like fight
sticks), **rumble** when your character is hit, and the stick deadzone.

Game & system has volume, mute/pause while the window is in the background,
the **CPU difficulty** (Extra easy to Extra hard; written to the disk's
adjustments exactly as the operator menu does, so it applies after Reset),
the DIP switch settings, auto-coin, run-ahead and rewind. F10 (or
Graphics → Performance overlay) shows frame rate, emulation time and speed,
and the audio buffer. The window's size
and position are remembered between runs.

## Training mode

Turn it on with F4 or in the menu. Health refills a moment after each
combo ends (so you can still read the damage) and the round clock stops.

For a sparring partner, start a two-player game (press 2 to join; auto-coin
pays), then:

1. **F8** — record: your controls now drive player 2 (up to 10 seconds).
2. **F8** — stop recording.
3. **F9** — the dummy repeats the recording on a loop; F9 again stops it.

Directions are recorded as pressed (left/right on screen), so record from
the side the dummy will be on.

F3 shows both players' recent inputs in numpad notation (6 = forward when
facing right, 2 = down, 5 = neutral) with the number of frames each was held.

## Netplay

Two players over the internet with rollback netcode: your own inputs
respond immediately, the opponent's are predicted and corrected, and the
game stays perfectly in sync. Linux and Windows can play each other.

- **Host:** menu → Netplay → *Host a game* (or `ki2 --host`). Your UDP port
  (`port` in ki2.ini, default 7845) must be reachable: forward it on your
  router, or both use a VPN such as Tailscale or ZeroTier.
- **Join:** menu → Netplay → *Address* (type the host's IP or name), then
  *Join a game* (or `ki2 --join HOST[:PORT]`).

Both players need the same build, ROM and disk image (checked when
connecting); the host's settings sectors and DIP switches are used. When
the other player connects, both games restart at the title screen. You
play with your usual controls whichever side you are on. The corner shows
ping, input delay and the last rollback.

**Input delay** (default 2 frames) trades a little responsiveness for
fewer corrections: 1-2 for nearby players, 3-4 across a continent.
During netplay the game cannot be paused, rewound or reloaded, and both
sides run at the arcade's 58.96 Hz.

## Save states, rewind and run-ahead

States go to `states/slot1.ki2s` … `slot9.ki2s` (about 6 MB each). They
include the disk's settings sectors, and only load into the same build and
ROM version.

Rewind keeps about 50 seconds by default (`rewind_mb = 256`).

KI2 shows the result of an input one frame after it is read. Run-ahead 1
hides that frame by quietly emulating one frame ahead each frame. It costs
about one extra frame of CPU time; 2 frames is possible on a fast CPU but
does not remove any more of this game's lag.

Everything above can be changed in `ki2.ini`, which is written with
comments on first run. It also has video scaling (`fit`, `integer`,
`stretch`), smoothing, frame pacing, audio latency and the boot skip.

## Frame pacing

The game runs at 58.96 Hz. With `pacing = auto` a ~60 Hz display is
driven by vsync for perfectly smooth motion (the game runs 1.7% fast to
match); other refresh rates are timed by the sound card at the original
speed.
