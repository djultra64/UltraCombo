#!/bin/sh
# Deterministic regression run: boot, start, a scripted two-player fight,
# 4400 frames headless; prints the state hash. Builds that emulate the same
# give the same hash (any compiler, Linux or Windows, linked-in code or a
# module built at run time).
#
#   tools/regress.sh [-g ki1|ki2] PROGRAM DISK_HEAD [RUN_DIR] [WORK_DIR]
#
# -g: which game's files and inputs to use (default ki2; must match PROGRAM).
# DISK_HEAD: the first 32 KB of a disk image saved earlier (the settings and
# audits the game writes; the hash depends on it). RUN_DIR holds your game
# files (default: run/). The disk is copied into WORK_DIR (default
# $TMPDIR/uc-regress) once, and its head is reset before every run, so your
# own disk image is never written. The DIP switches are the defaults unless
# KI2_DSW is set. Extra environment (KI2_LOAD_MODULE, KI2_BUILD, KI2_NOREC,
# KI2_DUMP_AT, KI2_WAV, ...) passes through.
set -e
game=ki2
if [ "$1" = -g ]; then game=$2; shift 2; fi
case $game in
ki1) rom=ki-l15d.u98 disk=kinst.raw ;;
ki2) rom=ki2-l14.u98 disk=kinst2.raw ;;
*) echo "unknown game $game" >&2; exit 1 ;;
esac
prog=$(realpath "$1") head=$(realpath "$2")
run=$(realpath "${3:-run}") work=${4:-${TMPDIR:-/tmp}/uc-regress}
inputs=$(realpath "$(dirname "$0")/regress-$game.txt")
export KI2_DSW=${KI2_DSW:-fffff1ff}     # blood high, demo sounds, finishing moves, free play
mkdir -p "$work"
[ -f "$work/$disk" ] || cp --reflink=auto "$run/$disk" "$work/$disk"
dd if="$head" of="$work/$disk" bs=32768 count=1 conv=notrunc 2>/dev/null
cd "$work"
"$prog" --rom "$run/$rom" --sound "$run" --disk "$work/$disk" \
    --headless 4400 --script "$(cat "$inputs")" 2>&1 | grep -E '^(rec:|state hash)'
