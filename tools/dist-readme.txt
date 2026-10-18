UltraCombo - unofficial native ports of the Killer Instinct arcade games
========================================================================

UltraCombo plays Killer Instinct (as "UltraCombo") and Killer Instinct 2
(as "UltraCombo 2"). With both games installed it asks which to play; with
one, it starts it straight away. Menu > Change game goes back to the choice.

This package does NOT contain the games. You need your own copies of the
arcade games' files. Nothing from them (no ROM data, no disk data, no game
code) is included here.

1. Put your game files in the "run" folder (see run/PUT-GAME-FILES-HERE.txt):
   the ROMs and MAME's CHD as they are, no conversion needed.
2. Start the program (UltraCombo.exe on Windows, ./ultracombo on Linux).

AppImage (Linux): make a folder named "run" beside UltraCombo-x86_64.AppImage
and put the game files in it; settings and the cache then go beside the
AppImage too. Without that folder, use ~/.local/share/UltraCombo/run/.

First launch: "PREPARING THE GAME FOR THIS PC"
----------------------------------------------
The first time you play each game (and each version of it), the program
boots your game files, translates the game's code into C and compiles it
into a native module for your machine. That takes about a minute and only
happens once; the result is kept in the "cache" folder. If you change the
game files it is done again.

It uses, in order: the Zig compiler bundled in tools/zig, a C compiler
already on your system (gcc or clang), or TinyCC (tools/tcc). TinyCC is
fast to build with but makes slower code; the game still runs at full
speed on most PCs. If none of them work the game runs on the slower
built-in interpreter, and a warning tells you why (see build.log in the
cache/src-* folder).

Settings (ki1.ini, ki2.ini), the games' own saves (settings and high scores, in
"saves"), screenshots, save states and the cache go next to the program if
that folder is writable, otherwise into your user data folder:
  Windows: %APPDATA%\UltraCombo\
  Linux:   ~/.local/share/UltraCombo/

Linux: needs glibc 2.29 or newer (distributions from about 2020 on). SDL2
is included, in lib/.

Press F1 in the game for the menu. README.md has the full manual.

Killer Instinct is a trademark of its owners. This project is not
affiliated with or endorsed by them.

Licences are in the "licenses" folder: UltraCombo itself (MIT), SDL2
(zlib), libchdr and its decompressors (BSD, MIT, public domain), Zig (MIT),
TinyCC (LGPL 2.1, used as a separate library; source:
see tools/tcc/SOURCE.txt), shaders (licence header in each file; those in
shaders/extra are GPL: xBRZ GPL 3.0, the others GPL 2.0 or later).
