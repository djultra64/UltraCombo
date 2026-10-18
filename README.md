# UltraCombo 2

A native port for the Killer Instinct 2 arcade game.

Runs the arcade game's own code (statically recompiled to C) on Linux and
Windows, with the Midway DCS sound board and IDE hard disk emulated. You
need your own copies of the game data; none are included.

## Setup

Put these in `run/` (or point `ki2.ini` at them):

- `ki2-l14.u98` — boot ROM
- `kinst2.raw` — the hard disk, extracted from the MAME CHD:
  `chdman extractraw -i kinst2.chd -o kinst2.raw`
- `ki2_l1.u10` … `ki2_l1.u13`, `ki2_l1.u33` … `ki2_l1.u36` — sound ROMs

### Checking your files

These are the files of MAME's `kinst2` and `kinst` sets (the dumps MAME
lists as good). Check yours with `md5sum` (Linux), `certutil -hashfile FILE
MD5` (Windows) or any ROM manager. Every ROM is 524,288 bytes.

**UltraCombo 2 (Killer Instinct 2):**

| File | What | MD5 | SHA1 |
|---|---|---|---|
| `ki2-l14.u98` | boot ROM, L1.4 | `8681c9d373394b17f3499e8902055dab` | `aa7a2a9d72a47dd0ea2ee7b2776b79288060b179` |
| `ki2_l1.u10` | sound | `fa0c7f9dc7ee0995913a4769996a7d80` | `acfc9460cd5df01403b7f00b2f68c2a8734ad6d3` |
| `ki2_l1.u11` | sound | `dc87ed16f9d95cb0aa0ea0117ef4be09` | `fe7fc78f1c60b15f2bbdc4c455f55cdf30f48ed4` |
| `ki2_l1.u12` | sound | `0b034258a8561ac4bfa339a781777c4a` | `9997a83432cb720f65b40a8af46f31a5d0d16d8e` |
| `ki2_l1.u13` | sound | `6d42f7de396c7f9eb760e51af4fcdca2` | `7ff164fc2f079d039921594be92208973d43aa03` |
| `ki2_l1.u33` | sound | `aae93e81ac881c56d0aecab2f59c8797` | `0ab6dbfb76e9fa2db072e287864ad1f9d514dd9b` |
| `ki2_l1.u34` | sound | `116cbfd189879a3a601f1f7ccb64285c` | `48456a7b6592c40bc9c664dcd2ee2cfd91942811` |
| `ki2_l1.u35` | sound | `ec3103d67a528412da9cb91a2729df65` | `24a3ff009c8a7f5a0bfcb198b8dcb5df365770d3` |
| `ki2_l1.u36` | sound | `a03144702e28a9e89e50c5a377cb47b3` | `0fca72c40067034939b984b4bf32972a5a6c26af` |
| `kinst2.raw` | disk, freshly extracted | `2563b089b316f2c8636d78af661ac656` | `ab0242233d2eaf9d907abe246a54e09a8a2561a5` |

**UltraCombo (Killer Instinct), in development:**

| File | What | MD5 | SHA1 |
|---|---|---|---|
| `ki-l15d.u98` | boot ROM, L1.5D (default) | `6a63e4bbdef8ea9f0d26f7244341ca08` | `607394d4ba1713f38c2cb5159303cace9cde991e` |
| `ki-l14.u98` | boot ROM, L1.4 (optional) | `aff07bdb5bf4763bf81e243077cf9761` | `07254f20707377f7195e64675eb6458e663c1a9a` |
| `u10-l1` | sound | `9ee2b2cdda7ede8811a57507ef61e07b` | `810d455df8f385d76143e9d7d048f2b555ff8bf0` |
| `u11-l1` | sound | `bce2d8cd20e94d4406c19fd55d798946` | `0595909cb667c38ac7c8c7bd0646b28899e27777` |
| `u12-l1` | sound | `14bb46cef4884f8df5df1fa985e6d3bf` | `7a8ee405c118fd176b66353fa7bfab888cc63cd2` |
| `u13-l1` | sound | `ca0904ee517845eed5509426cf5d8560` | `ea4d1f153eb284f1bcfc5295fbce316bba6083f4` |
| `u33-l1` | sound | `da9e7f5f70320b51e6fdc23e9337484c` | `b22e365bc8d58a80eaac226be14b4bb8d9a04844` |
| `u34-l1` | sound | `207395792342705bea307ecb321a95bf` | `ee439f5162a2b3379d3f802328017bb3c68547d2` |
| `u35-l1` | sound | `a1758b43b42b32c25c770dc3ac7badc3` | `48c4c954ac9db648f28ad64f9845e19ec432eec3` |
| `u36-l1` | sound | `57b73bec5d0e8c90d8898e102ce4066b` | `cc78070cc41701e9a91fde5cfbdc7e1e83354854` |
| `kinst.raw` | disk, freshly extracted | `6d4c2f152c9a18ab3a9b05b8804306a8` | `a37a2c5e52ea936a715210d237874dd573bb002f` |

The disk checksums are for the image right after extraction: the game writes
its settings and high scores into the first sectors, so after the first
launch the `.raw` file no longer matches. To check a disk, check the CHD
instead. A CHD file's own MD5 depends on the chdman version that made it, so
compare the SHA1 that `chdman info -i FILE` prints as "SHA1" (the one MAME
uses):

| CHD | SHA1 (from `chdman info`) | MD5 of MAME's current file |
|---|---|---|
| `kinst2.chd` | `e7c9291b4648eae0012ea0cc230731ed4987d1d5` | `5970ca942c0c7c5f62f651bf93e15dc5` |
| `kinst.chd` | `81d833236e994528d1482979261401b198d1ca53` | `a542547abdff0a28df440c3a4d1cfdde` |

Then start `ultracombo2` (`UltraCombo2.exe` on Windows). On the first launch
the program shows "PREPARING THE GAME FOR THIS PC": it boots your game files
on its interpreter until the game has loaded its code, snapshots that code,
translates it to C and compiles it into a native module. That takes about a
minute, once; the module is cached in `cache/` (a new one is made if the
game files change). The compiler used is the first that works of:

1. Zig, bundled in `tools/zig` — what the release packages use
2. a C compiler already installed (`cc`, `gcc` or `clang`)
3. TinyCC, bundled in `tools/tcc` — builds in seconds, but its code runs at
   roughly half the speed (still well above full speed on most PCs)

If none works the game runs on the interpreter, with a warning; see
`build.log` in `cache/src-*/`. All three produce code that behaves identically, so
netplay works between them.

Settings, the log, screenshots, states and the cache are written next to the
program if that folder is writable, else into the user data folder
(`%APPDATA%\UltraCombo2\` on Windows, `~/.local/share/UltraCombo2/` on
Linux), whose `run/` folder is also searched for the game files.

### Building

Needs gcc, SDL2 and python3:

    make            # the program; builds the game code at first launch
    make recomp     # developers: link the game code in instead (a minute)
    ./ultracombo2

`make recomp` puts the generated C in `build/rec/`, derived from your ROM;
that binary contains game code and must not be shared.

### Windows

Cross-compiled from Linux with MinGW-w64 (`gcc-mingw-w64-x86-64`) and the
official SDL2 MinGW package, unpacked into `deps/sdl2-mingw/`:

    make windows    # -> dist/windows/UltraCombo2.exe + SDL2.dll

On Windows the log goes to `ki2.log` beside the executable. Linux and
Windows builds emulate identically (same state after the same inputs).

### Release packages

    make packages   # -> dist/UltraCombo2-Linux.tar.xz, dist/UltraCombo2-x86_64.AppImage,
                    #    dist/UltraCombo2-Windows.zip

These hold the program, Zig and TinyCC, shaders and licences — no game files
and no game code. They expect, in `deps/`: Zig 0.17 for Linux and Windows
(`zig-linux/`, `zig-windows/`), TinyCC's `libtcc.so` built with that Zig
against glibc 2.31 (`tcc-lnx231/`) and `libtcc.dll` (`tcc-win/`). The Linux
program is built with Zig against glibc 2.31 too, so it runs on distributions
from about 2020 on, and so is SDL2 2.32.10 (from its source release, into
`deps/sdl2-lnx/build`; configure with `-DCMAKE_LIBRARY_ARCHITECTURE=x86_64-linux-gnu`
and `-idirafter /usr/include` so it finds the X11/Wayland/audio headers),
which ships in `lib/`.

    make package-appimage   # -> dist/UltraCombo2-x86_64.AppImage

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
are remembered. `ultracombo2 --test-shaders` checks that every preset compiles.

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

- **Host:** menu → Netplay → *Host a game* (or `ultracombo2 --host`). Your UDP port
  (`port` in ki2.ini, default 7845) must be reachable: forward it on your
  router, or both use a VPN such as Tailscale or ZeroTier.
- **Join:** menu → Netplay → *Address* (type the host's IP or name), then
  *Join a game* (or `ultracombo2 --join HOST[:PORT]`).

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

## Licence

UltraCombo 2 is MIT-licensed (`LICENSE`). That covers this project's own code
only: the game, its code and its data belong to their owners, and you need
your own copy. Bundled third-party parts keep their licences: stb_image
(public domain / MIT), the shaders in `shaders/` (see `shaders/README.txt`),
and in the release packages SDL2 (zlib), Zig (MIT) and TinyCC (LGPL 2.1).
