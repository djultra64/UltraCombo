#!/usr/bin/env python3
"""Build the Lola Retro arcade presets for our shader loader.

usage: lola_retro.py LOLA_REPO DEST

LOLA_REPO is a checkout of https://github.com/zoran123456/lola-retro (MIT).
Its tools/make_glsl.py converts the RetroArch slang passes to GLSL for
Amiberry; this adds one more target with the same conversion: the
arcade-15khz system preset (the 1084S tube with arcade timing), curved and
flat. Differences from the Amiberry target:
- no LR_FIELDS: our frame has each line once, not doubled;
- no LR_ROWS_UP: inside our chain the top row comes first, as in RetroArch;
- no fields passes: beam feeds crt directly, as in RetroArch and 86Box;
- wrap_modeN means how pass N reads its input, as in RetroArch (Amiberry
  gives it to pass N's output, so make_glsl moves the values one pass up).
LR_NO_FEEDBACK (no afterglow) and LR_FIT_43 (tube stays 4:3) stay.
"""
import re
import shutil
import sys
from pathlib import Path

repo, dest = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
sys.path.insert(0, str(repo / "tools"))
import make_glsl as mg              # noqa: E402
from make_presets import SYSTEMS    # noqa: E402

tube, system = SYSTEMS["arcade-15khz"]
target = dict(
    api="amiberry",
    preset=tube,
    out=dest,
    title="Arcade monitor (15 kHz)",
    emulator="UltraCombo",
    defines=["LR_NO_FEEDBACK", "LR_FIT_43"],
    params=mg.AMIGA_PARAMS,
    fixed=dict(system),
    passes=mg.CHAIN[:5] + [mg.CRT, mg.CHAIN[6]],
)

table = mg.parameter_table()
values = {n: d[1] for n, d in table.items()}
values.update(mg.BASE)
values.update(mg.PRESETS[tube])
values.update(target["fixed"])
# The 1084S overscan (0.1) suits boards that leave the field's edge lines
# black; these games draw all 240 lines, so the health bars and the fighters'
# feet would run off the glass. A thin border shows the whole picture.
values["LR_OVERSCAN"] = -0.03

CREDIT = "Copyright (c) 2026 Zoki. MIT licence, see LICENSE. https://github.com/zoran123456/lola-retro"


def credited(text, comment):
    """The file with the author's line after its first line (#version must stay first)."""
    first, rest = text.split("\n", 1)
    return f"{first}\n{comment} {CREDIT}\n{rest}"


(dest / "shaders").mkdir(parents=True, exist_ok=True)
for i, p in enumerate(target["passes"]):
    (dest / "shaders" / f"{p['src']}.glsl").write_text(credited(mg.convert(i, target, values, table), "//"))
for flat in (False, True):
    vals = {**values, **(mg.FLAT if flat else {})}
    diff = {n: v for n, v in vals.items() if n in table and float(v) != float(values[n])}
    assert all(n in target["params"] for n in diff), diff
    name = "arcade-15khz" + ("-flat" if flat else "")
    text = mg.preset_text(target, target["title"] + (", flat" if flat else ""), diff)
    text = re.sub(r"^wrap_mode(\d+) = .*$", lambda m: f"wrap_mode{m[1]} = {target['passes'][int(m[1])]['wrap']}",
                  text, flags=re.M)
    (dest / f"{name}.glslp").write_text(credited(text, "#"))
shutil.copy(repo / "LICENSE", dest / "LICENSE")
print("wrote", dest)
