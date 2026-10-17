#!/usr/bin/env python3
"""Copy libretro GLSL presets and everything they use into a shader folder.

usage: install_shaders.py SOURCE_REPO DEST preset.glslp [preset.glslp ...]

SOURCE_REPO is a checkout of https://github.com/libretro/glsl-shaders.
Pass files, lookup textures and #reference'd presets are copied too, keeping
their paths relative to the repository so the presets still resolve.
"""
import os
import re
import shutil
import sys


def deps(path):
    d = os.path.dirname(path)
    out = []
    textures = []
    for line in open(path, errors='ignore'):
        line = line.strip()
        m = re.match(r'#reference\s+"([^"]+)"', line)
        if m:
            ref = os.path.normpath(os.path.join(d, m.group(1)))
            out.append(ref)
            out += deps(ref)
            continue
        m = re.match(r'(\w+)\s*=\s*"?([^"]*)"?\s*$', line)
        if not m:
            continue
        key, val = m.group(1), m.group(2).strip()
        if re.fullmatch(r'shader\d+', key):
            out.append(os.path.normpath(os.path.join(d, val)))
        elif key == 'textures':
            textures += [t.strip() for t in val.split(';') if t.strip()]
        elif key in textures:
            out.append(os.path.normpath(os.path.join(d, val)))
    # textures can be defined before the list that names them
    for line in open(path, errors='ignore'):
        m = re.match(r'\s*(\w+)\s*=\s*"?([^"]*)"?\s*$', line)
        if m and m.group(1) in textures:
            out.append(os.path.normpath(os.path.join(d, m.group(2).strip())))
    return out


def main():
    src, dst = sys.argv[1], sys.argv[2]
    copied = 0
    for preset in sys.argv[3:]:
        files = [os.path.normpath(os.path.join(src, preset))]
        files += deps(files[0])
        for f in dict.fromkeys(files):
            rel = os.path.relpath(f, src)
            if rel.startswith('..') or not os.path.isfile(f):
                print('missing:', rel, file=sys.stderr)
                continue
            os.makedirs(os.path.dirname(os.path.join(dst, rel)), exist_ok=True)
            shutil.copy2(f, os.path.join(dst, rel))
            copied += 1
    print('installed %d files into %s' % (copied, dst))


if __name__ == '__main__':
    main()
