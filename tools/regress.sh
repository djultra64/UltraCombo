#!/bin/sh
# Deterministic regression run: boot, coin up, a scripted two-player fight,
# 4400 frames headless; prints the state hash. Builds that emulate the same
# give the same hash (any compiler, Linux or Windows, linked-in code or a
# module built at run time).
#
#   tools/regress.sh PROGRAM DISK_HEAD [RUN_DIR] [WORK_DIR]
#
# DISK_HEAD: the first 32 KB of a disk image saved earlier (the settings and
# audits the game writes; the hash depends on it). RUN_DIR holds your game
# files (default: run/). The disk is copied into WORK_DIR (default
# $TMPDIR/uc2-regress) once, and its head is reset before every run, so your
# own disk image is never written. Extra environment (KI2_LOAD_MODULE,
# KI2_BUILD, KI2_NOREC, ...) passes through.
set -e
prog=$(realpath "$1") head=$(realpath "$2")
run=$(realpath "${3:-run}") work=${4:-${TMPDIR:-/tmp}/uc2-regress}
inputs=$(realpath "$(dirname "$0")/regress-inputs.txt")
mkdir -p "$work"
[ -f "$work/disk.raw" ] || cp --reflink=auto "$run/kinst2.raw" "$work/disk.raw"
dd if="$head" of="$work/disk.raw" bs=32768 count=1 conv=notrunc 2>/dev/null
cd "$work"
"$prog" --rom "$run/ki2-l14.u98" --sound "$run" --disk "$work/disk.raw" \
    --headless 4400 --script "$(cat "$inputs")" 2>&1 | grep -E '^(rec:|state hash)'
