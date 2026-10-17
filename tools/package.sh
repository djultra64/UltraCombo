#!/bin/sh
# Assemble a distributable package: the program, the compilers it uses to
# build the game code on the player's machine, shaders and licences.
# No game files and no game code go in.
#
#   tools/package.sh linux   PROGRAM          -> dist/KI2-Port-Linux/
#   tools/package.sh windows PROGRAM SDL2.dll -> dist/KI2-Port-Windows/
set -e
target=$1 prog=$2
case $target in
linux)   out=dist/KI2-Port-Linux   zig=deps/zig-linux   tcc=deps/tcc-lnx231 ;;
windows) out=dist/KI2-Port-Windows zig=deps/zig-windows tcc=deps/tcc-win ;;
*) echo "usage: $0 linux|windows PROGRAM [SDL2.dll]" >&2; exit 1 ;;
esac

rm -rf "$out"
mkdir -p "$out/run" "$out/tools/zig/lib" "$out/tools/tcc" "$out/licenses"
cp "$prog" "$out/"
cp README.md "$out/"
cp tools/dist-readme.txt "$out/READ-ME-FIRST.txt"
cp tools/run-readme.txt "$out/run/PUT-GAME-FILES-HERE.txt"

# Zig: only what "zig cc" needs to build a module without the C library.
# Windows modules still link against MinGW's runtime, so keep that there.
if [ $target = windows ]; then
    cp "$zig/zig.exe" "$out/tools/zig/"
    mkdir -p "$out/tools/zig/lib/libc/include"
    cp -r "$zig/lib/libc/mingw" "$out/tools/zig/lib/libc/"
    cp -r "$zig/lib/libc/include/any-windows-any" "$out/tools/zig/lib/libc/include/"
else
    cp "$zig/zig" "$out/tools/zig/"
fi
cp -r "$zig/lib/std" "$zig/lib/compiler_rt" "$zig/lib/include" "$zig/lib/c" "$out/tools/zig/lib/"
cp "$zig"/lib/*.zig "$out/tools/zig/lib/"
cp "$zig/LICENSE" "$out/licenses/Zig-LICENSE.txt"

# TinyCC, loaded as a separate library (LGPL)
if [ $target = windows ]; then
    cp "$tcc/libtcc.dll" "$tcc/win32/lib/chkstk.S" "$out/tools/tcc/"
else
    cp "$tcc/libtcc.so" "$out/tools/tcc/"
fi
cp "$tcc/COPYING" "$out/licenses/TinyCC-COPYING.txt"
echo "TinyCC source: https://repo.or.cz/tinycc.git, commit e23dabe (built unmodified)" > "$out/tools/tcc/SOURCE.txt"

# shaders; the GPL extras are shipped as the source files they are
cp -r shaders "$out/"
if [ -d shaders/extra ]; then
    cp tools/gpl-2.0.txt "$out/licenses/GPL-2.0-shaders-extra.txt"   # crt-guest, crt-royale, ...
    cp tools/gpl-3.0.txt "$out/licenses/GPL-3.0-shaders-extra.txt"   # xBRZ
fi

if [ $target = windows ]; then
    cp "$3" "$out/"
    sdl=$(dirname "$3")/../../README-SDL.txt
    [ -f "$sdl" ] && cp "$sdl" "$out/licenses/SDL2-README.txt"
    echo "SDL2 is distributed under the zlib licence: https://libsdl.org/license.php" > "$out/licenses/SDL2-LICENSE.txt"
    # Windows text files want CRLF
    for f in "$out"/READ-ME-FIRST.txt "$out"/run/PUT-GAME-FILES-HERE.txt; do
        sed -i 's/$/\r/' "$f"
    done
fi
echo "packaged $out ($(du -sh "$out" | cut -f1))"
