# Killer Instinct 2 — native port

Runs the arcade game's own code (statically recompiled to C) on Linux, with
the Midway DCS sound board and IDE hard disk emulated. You need your own
copies of the game data; none are included.

## Setup

Put these in `run/` (or point `ki2.ini` at them):

- `ki2-l14.u98` — boot ROM
- `kinst2.raw` — the hard disk, extracted from the MAME CHD:
  `chdman extractraw -i kinst2.chd -o kinst2.raw`
- `ki2_l1.u10` … `ki2_l1.u13`, `ki2_l1.u33` … `ki2_l1.u36` — sound ROMs

Build (needs gcc, SDL2, python3):

    make            # interpreter only
    make recomp     # boot once, snapshot the game code, recompile it, rebuild
    ./ki2

`make recomp` takes about a minute and only needs redoing after `make clean`.
The generated C stays in `build/` because it is derived from your ROM.

## Controls

| Action | Player 1 | Player 2 | Pad (Xbox names) |
|---|---|---|---|
| Move | Arrow keys | I J K L | D-pad / left stick |
| Quick / Medium / Fierce punch | A S D | T Y U | X / Y / RB |
| Quick / Medium / Fierce kick | Z X C | G H B | A / B / RT |
| Start | 1 | 2 | Start |
| Coin | 5 | 6 | Back |

With `auto_coin` on (the default), Start inserts the coins it needs.
The first pad connected is player 1, the second player 2.

| Hotkey | |
|---|---|
| P | Pause |
| . | Frame advance (pauses first) |
| Tab (hold) | Fast-forward |
| F11 or Alt+Enter | Fullscreen (Esc leaves it) |
| F12 | Screenshot to `screenshots/` |
| = / - / M | Volume up / down / mute |
| F2 | Operator test menu |
| 9 | Service credit |
| Page Up / Page Down | The cabinet's own volume buttons |
| Esc | Quit |

Everything above can be changed in `ki2.ini`, which is written with
comments on first run. It also has video scaling (`fit`, `integer`,
`stretch`), smoothing, frame pacing, audio latency and the boot skip.

## Frame pacing

The game runs at 58.96 Hz. With `pacing = auto` a ~60 Hz display is
driven by vsync for perfectly smooth motion (the game runs 1.7% fast to
match); other refresh rates are timed by the sound card at the original
speed.
