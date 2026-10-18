#!/usr/bin/env python3
"""Static recompiler: translate KI2's RAM-resident MIPS code to C.

usage: recomp.py RAM0.bin RAM2.bin OUTDIR [COVER.bin]

RAM0/RAM2 are snapshots taken after the game has loaded its code. COVER is an
optional KI2_COVER dump from an interpreter run; its jump targets become extra
entry points. The output (OUTDIR/rec_*.c) is compiled into ultracombo by the Makefile.

Code is found by recursive descent from the reset entry and the exception
vector, plus any word in RAM that points into the code (return addresses,
function pointers, jump tables). JAL targets split the code into functions;
each becomes one C function with a label for every address the dispatcher
may need to enter at.
"""
import os
import struct
import sys

BASE = 0x88000000          # code is linked to run from KSEG0 RAM at phys 0x08000000
RAM0_SIZE = 0x80000
VECTOR = 0x80000180
FUNCS_PER_FILE = 150


def s16(x):
    return x - 0x10000 if x & 0x8000 else x


class Image:
    def __init__(self, ram0, ram2):
        self.ram0, self.ram2 = ram0, ram2

    def word(self, va):
        pa = va & 0x1fffffff
        if pa < RAM0_SIZE:
            return struct.unpack_from('<I', self.ram0, pa)[0]
        if 0x08000000 <= pa < 0x08000000 + len(self.ram2):
            return struct.unpack_from('<I', self.ram2, pa - 0x08000000)[0]
        return None


def flow(pc, op):
    """Control flow of one instruction.

    Returns (kind, target, link) where kind is None (not a branch), 'b'
    (conditional), 'bl' (branch likely), 'j' (unconditional static),
    'jal' (call), 'jr' / 'jalr' (register), 'eret'.
    """
    o = op >> 26
    rs, rt = (op >> 21) & 31, (op >> 16) & 31
    btgt = (pc + 4 + (s16(op & 0xffff) << 2)) & 0xffffffff
    if o == 0:
        f = op & 63
        if f == 0x08:
            return ('jr', None, False)
        if f == 0x09:
            return ('jalr', None, True)
        return (None, None, False)
    if o == 1:
        if rt in (0x00, 0x01):
            return ('b', btgt, False)
        if rt in (0x02, 0x03):
            return ('bl', btgt, False)
        if rt in (0x10, 0x11):
            return ('b', btgt, True)
        if rt in (0x12, 0x13):
            return ('bl', btgt, True)
        return (None, None, False)
    if o == 2:
        return ('j', ((pc + 4) & 0xf0000000) | ((op & 0x3ffffff) << 2), False)
    if o == 3:
        return ('jal', ((pc + 4) & 0xf0000000) | ((op & 0x3ffffff) << 2), True)
    if o in (4, 5, 6, 7):
        if o == 4 and rs == 0 and rt == 0:
            return ('j', btgt, False)
        return ('b', btgt, False)
    if o in (0x14, 0x15, 0x16, 0x17):
        return ('bl', btgt, False)
    if o == 0x11 and rs == 8:
        return ('bl' if (op >> 17) & 1 else 'b', btgt, False)
    if o == 0x10 and rs == 8:            # BC0: treated as never-true by the interpreter
        return ('bl' if (op >> 17) & 1 else 'b', btgt, False)
    if o == 0x10 and rs >= 0x10 and (op & 63) == 0x18:
        return ('eret', None, False)
    return (None, None, False)


def stops_here(op):
    """Instructions handed to the interpreter that may need to return to the
    dispatcher afterwards (COP0: interrupts, idle loops, ERET)."""
    return (op >> 26) == 0x10


class Program:
    def __init__(self, img, cover=None):
        self.img = img
        self.code = set()          # addresses reached by descent
        self.leaders = set()
        self.calls = set()         # JAL targets
        self.cover = cover

    def descend(self, entries, lo, hi):
        work = [e for e in entries if lo <= e < hi]
        while work:
            pc = work.pop()
            while lo <= pc < hi and pc not in self.code:
                op = self.img.word(pc)
                if op is None:
                    break
                self.code.add(pc)
                kind, tgt, link = flow(pc, op)
                if stops_here(op):
                    self.leaders.add(pc + 4)
                if kind is None:
                    pc += 4
                    continue
                self.code.add(pc + 4)       # delay slot
                if tgt is not None:
                    self.leaders.add(tgt)
                    work.append(tgt)
                if kind == 'jal':
                    self.calls.add(tgt)
                if link or kind in ('b', 'bl'):
                    self.leaders.add(pc + 8)
                    pc += 8
                    continue
                break                       # j / jr / eret: no fall-through


def main():
    ram0 = open(sys.argv[1], 'rb').read()
    ram2 = open(sys.argv[2], 'rb').read()
    outdir = sys.argv[3]
    cover = open(sys.argv[4], 'rb').read() if len(sys.argv) > 4 else None
    img = Image(ram0, ram2)
    prog = Program(img)

    entries = {BASE}
    # the general exception vector jumps into the game's handler
    for pc in range(VECTOR, VECTOR + 0x40, 4):
        kind, tgt, _ = flow(pc, img.word(pc))
        if tgt is not None and (tgt & 0xff000000) == (BASE & 0xff000000):
            entries.add(tgt)
    if cover:
        half = len(cover) // 2
        for bi in range(half):
            b = cover[half + bi]
            if not b:
                continue
            for k in range(8):
                if b >> k & 1:
                    off = (bi * 8 + k) * 4 - RAM0_SIZE
                    if off >= 0:
                        entries.add(BASE + off)
    prog.leaders |= entries

    limit = BASE + len(ram2)
    prog.descend(entries, BASE, limit)
    end = max(prog.code) + 4
    # Pointers into the code found anywhere in RAM (return addresses, function
    # pointers, jump tables) become entry points. They may not extend the
    # region: past its end lies data, and stores there would invalidate it.
    words = struct.unpack('<%dI' % (len(ram0) // 4), ram0) + struct.unpack('<%dI' % (len(ram2) // 4), ram2)
    ptrs = {w for w in words if BASE <= w < end and not w & 3}
    prog.leaders |= ptrs
    prog.descend(ptrs, BASE, end)
    nwords = (end - BASE) // 4 + 1                      # +1: delay slot of the last word

    starts = sorted({BASE} | {c for c in prog.calls if BASE <= c < end})
    funcs = []
    for i, s in enumerate(starts):
        e = starts[i + 1] if i + 1 < len(starts) else end
        funcs.append((s, e))

    leaders = {l for l in prog.leaders if BASE <= l < end}
    os.makedirs(outdir, exist_ok=True)
    for f in os.listdir(outdir):
        if f.startswith('rec_') and f.endswith('.c'):
            os.remove(os.path.join(outdir, f))

    labels = []                                       # (pc, func index)
    bodies = []
    for fi, (lo, fhi) in enumerate(funcs):
        body, entry = gen_function(img, lo, fhi, end, leaders | {lo})
        bodies.append(body)
        labels += [(pc, fi) for pc in entry]

    nfiles = (len(funcs) + FUNCS_PER_FILE - 1) // FUNCS_PER_FILE
    for n in range(nfiles):
        with open(os.path.join(outdir, 'rec_%03d.c' % n), 'w') as out:
            out.write('// Generated by tools/recomp.py from the game\'s code. Do not edit.\n')
            out.write('#include "rec_gen.h"\n\n')
            for fi in range(n * FUNCS_PER_FILE, min(len(funcs), (n + 1) * FUNCS_PER_FILE)):
                out.write(bodies[fi])

    with open(os.path.join(outdir, 'rec_tab.c'), 'w') as out:
        out.write('// Generated by tools/recomp.py. Do not edit.\n#include "rec.h"\n\n')
        for lo, _ in funcs:
            out.write('void f_%08x(void);\n' % lo)
        out.write('\nconst rec_func_t rec_funcs[] = {\n')
        for lo, fhi in funcs:
            # validate through the delay slot of the last instruction
            out.write('    { f_%08x, 0x%08x, 0x%08x },\n' % (lo, lo, min(fhi + 4, BASE + nwords * 4)))
        out.write('};\nconst uint32_t rec_nfuncs = %d;\n\n' % len(funcs))
        labels.sort()
        out.write('const rec_label_t rec_labels[] = {\n')
        for pc, fi in labels:
            out.write('    { 0x%08x, %d },\n' % (pc, fi))
        out.write('};\nconst uint32_t rec_nlabels = %d;\n\n' % len(labels))
        out.write('const uint32_t rec_image_base = 0x%08x, rec_image_words = %d;\n' % (BASE, nwords))
        out.write('const uint32_t rec_image[] = {\n')
        for i in range(0, nwords, 8):
            out.write('    ' + ', '.join('0x%08x' % img.word(BASE + 4 * j) for j in range(i, min(nwords, i + 8))) + ',\n')
        out.write('};\n')

    reached = len(prog.code)
    print('recomp: %d KB of code at %08x-%08x, %d functions, %d entry points, %d words reached by descent'
          % ((end - BASE) // 1024, BASE, end, len(funcs), len(labels), reached))


# ---------------------------------------------------------------------------
# Code generation

def reg(n):
    return 'R[%d]' % n


def wr(n, expr):
    return '%s = %s;' % (reg(n), expr) if n else ''


def gen_op(pc, op, dl):
    """C for one non-branch instruction. dl: it executes in a delay slot."""
    o = op >> 26
    rs, rt, rd, sa = (op >> 21) & 31, (op >> 16) & 31, (op >> 11) & 31, (op >> 6) & 31
    imm = s16(op & 0xffff)
    uimm = op & 0xffff
    D = 'true' if dl else 'false'
    P = '0x%08xu' % pc
    interp = 'if (rec_op(0x%08xu, %s, %s)) return;' % (op, P, D)
    RS, RT = reg(rs), reg(rt)
    ea = '(uint32_t)(%s + %dll)' % (RS, imm)

    if o == 0:
        f = op & 63
        simple = {
            0x00: 'SX32((uint32_t)%s << %d)' % (RT, sa),
            0x02: 'SX32((uint32_t)%s >> %d)' % (RT, sa),
            0x03: 'SX32((int32_t)%s >> %d)' % (RT, sa),
            0x04: 'SX32((uint32_t)%s << (%s & 31))' % (RT, RS),
            0x06: 'SX32((uint32_t)%s >> (%s & 31))' % (RT, RS),
            0x07: 'SX32((int32_t)%s >> (%s & 31))' % (RT, RS),
            0x10: 'cpu.hi', 0x12: 'cpu.lo',
            0x14: '%s << (%s & 63)' % (RT, RS),
            0x16: '%s >> (%s & 63)' % (RT, RS),
            0x17: '(uint64_t)((int64_t)%s >> (%s & 63))' % (RT, RS),
            0x21: 'SX32((uint32_t)%s + (uint32_t)%s)' % (RS, RT),
            0x23: 'SX32((uint32_t)%s - (uint32_t)%s)' % (RS, RT),
            0x24: '%s & %s' % (RS, RT), 0x25: '%s | %s' % (RS, RT),
            0x26: '%s ^ %s' % (RS, RT), 0x27: '~(%s | %s)' % (RS, RT),
            0x2a: '(uint64_t)((int64_t)%s < (int64_t)%s)' % (RS, RT),
            0x2b: '(uint64_t)(%s < %s)' % (RS, RT),
            0x2d: '%s + %s' % (RS, RT), 0x2f: '%s - %s' % (RS, RT),
            0x38: '%s << %d' % (RT, sa), 0x3a: '%s >> %d' % (RT, sa),
            0x3b: '(uint64_t)((int64_t)%s >> %d)' % (RT, sa),
            0x3c: '%s << %d' % (RT, sa + 32), 0x3e: '%s >> %d' % (RT, sa + 32),
            0x3f: '(uint64_t)((int64_t)%s >> %d)' % (RT, sa + 32),
        }
        if f in simple:
            return wr(rd, simple[f])
        if f == 0x0f:
            return ''                                   # SYNC
        if f == 0x11:
            return 'cpu.hi = %s;' % RS
        if f == 0x13:
            return 'cpu.lo = %s;' % RS
        muldiv = {0x18: 'mult', 0x19: 'multu', 0x1a: 'div', 0x1b: 'divu',
                  0x1c: 'dmult', 0x1d: 'dmultu', 0x1e: 'ddiv', 0x1f: 'ddivu'}
        if f in muldiv:
            return 'rec_%s(%s, %s);' % (muldiv[f], RS, RT)
        if f in (0x20, 0x22):                           # ADD / SUB: trap on overflow
            fn = 'add' if f == 0x20 else 'sub'
            return '{ int32_t _r; if (__builtin_%s_overflow((int32_t)%s, (int32_t)%s, &_r)) { %s } %s }' % (
                fn, RS, RT, interp, wr(rd, 'SX32(_r)'))
        return interp
    if o == 0x08:                                       # ADDI
        return '{ int32_t _r; if (__builtin_add_overflow((int32_t)%s, %d, &_r)) { %s } %s }' % (RS, imm, interp, wr(rt, 'SX32(_r)'))
    simple = {
        0x09: 'SX32((uint32_t)%s + %du)' % (RS, imm & 0xffffffff),
        0x0a: '(uint64_t)((int64_t)%s < %dll)' % (RS, imm),
        0x0b: '(uint64_t)(%s < 0x%xull)' % (RS, imm & 0xffffffffffffffff),
        0x0c: '%s & 0x%xu' % (RS, uimm), 0x0d: '%s | 0x%xu' % (RS, uimm),
        0x0e: '%s ^ 0x%xu' % (RS, uimm),
        0x0f: '0x%xull' % ((((uimm << 16) ^ 0x80000000) - 0x80000000) & 0xffffffffffffffff),
        0x19: '%s + %dll' % (RS, imm),
    }
    if o in simple:
        return wr(rt, simple[o])
    loads = {0x20: ('int8_t', 1, '(uint64_t)(int64_t)_v'), 0x21: ('int16_t', 2, '(uint64_t)(int64_t)_v'),
             0x23: ('uint32_t', 4, 'SX32(_v)'), 0x24: ('uint8_t', 1, '_v'), 0x25: ('uint16_t', 2, '_v'),
             0x27: ('uint32_t', 4, '_v'), 0x37: ('uint64_t', 8, '_v')}
    if o in loads:
        t, size, val = loads[o]
        dst = '%s = %s' % (RT, val) if rt else '(void)_v'
        return 'LD(%s, %s, %s, %d, %s, %s);' % (t, dst, ea, size, P, D)
    stores = {0x28: ('uint8_t', 1), 0x29: ('uint16_t', 2), 0x2b: ('uint32_t', 4), 0x3f: ('uint64_t', 8)}
    if o in stores:
        t, size = stores[o]
        return 'ST(%s, %s, %s, %d, %s, %s);' % (t, RT, ea, size, P, D)
    if o == 0x2f:
        return ''                                       # CACHE
    unaligned = {0x22: 'lwl', 0x26: 'lwr', 0x1a: 'ldl', 0x1b: 'ldr'}
    if o in unaligned:
        put = ' %s = _t;' % RT if rt else ''
        return '{ uint64_t _t = %s; if (!rec_%s(&_t, %s, %s, %s)) return;%s }' % (RT, unaligned[o], ea, P, D, put)
    unaligned = {0x2a: 'swl', 0x2e: 'swr', 0x2c: 'sdl', 0x2d: 'sdr'}
    if o in unaligned:
        return 'if (!rec_%s(%s, %s, %s, %s)) return;' % (unaligned[o], RT, ea, P, D)
    if o == 0x10 and rs in (0, 1) and rd not in (1, 13):
        # MFC0/DMFC0 except Random and Cause (Cause polling is how idle loops are found)
        if rd == 9:
            val = 'SX32(cpu.count_base + (uint32_t)((cpu.cycles - cpu.count_cyc) / 2))'
        else:
            val = 'SX32(cpu.cp0[%d])' % rd if rs == 0 else 'cpu.cp0[%d]' % rd
        return 'if (!rec_cop0_usable()) { rec_op(0x%08xu, %s, %s); return; } %s' % (op, P, D, wr(rt, val))
    return interp                                       # COP0/1, unaligned, LL/SC, traps...


def gen_cond(op):
    o = op >> 26
    rs, rt = (op >> 21) & 31, (op >> 16) & 31
    RS, RT = reg(rs), reg(rt)
    if o == 1:
        return '(int64_t)%s %s 0' % (RS, '<' if rt in (0x00, 0x02, 0x10, 0x12) else '>=')
    if o in (4, 0x14):
        return '%s == %s' % (RS, RT)
    if o in (5, 0x15):
        return '%s != %s' % (RS, RT)
    if o in (6, 0x16):
        return '(int64_t)%s <= 0' % RS
    if o in (7, 0x17):
        return '(int64_t)%s > 0' % RS
    if o == 0x11:
        return '%s((cpu.fcr31 >> 23) & 1)' % ('' if (op >> 16) & 1 else '!')
    if o == 0x10:
        return '%s' % ('false' if (op >> 16) & 1 else 'true')
    raise ValueError(op)


def gen_function(img, lo, hi, end, leaders):
    """C for the MIPS function occupying [lo, hi). Returns (text, entry pcs)."""
    flows = {}
    for pc in range(lo, hi + 4, 4):
        flows[pc] = flow(pc, img.word(pc))
    # Computed jumps (jr through anything but ra) usually land in unrolled
    # code at an offset worked out at run time: make every address enterable.
    computed = any(flows[pc][0] == 'jr' and (img.word(pc) >> 21) & 31 != 31 for pc in range(lo, hi, 4))
    if computed:
        leaders = leaders | set(range(lo, hi, 4))
    local = sorted(l for l in leaders if lo <= l < hi)
    lset = set(local)
    # A branch whose delay slot is another branch is left to the interpreter.
    bad = {pc for pc in range(lo, hi, 4) if flows[pc][0] and flows[pc + 4][0]}
    entries = [l for l in local if l not in bad]
    # When a delay slot is also a label its code is emitted on its own, so the
    # not-taken path of the branch needs a label to jump over it.
    split = {pc + 8 for pc in range(lo, hi, 4)
             if flows[pc][0] in ('b', 'bl') and pc + 4 in lset and pc + 8 < hi}
    lset |= split

    def seg_len(s):
        k = s
        while k < hi:
            if k != s and k in lset:
                return (k - s) // 4
            if k in bad:
                return (k - s) // 4
            if flows[k][0]:
                return (k - s) // 4 + 2
            k += 4
        return (k - s) // 4

    def jump(t, pc):
        if lo <= t < hi and t in lset and t not in bad:
            if t <= pc:
                return 'if (STOP()) EXIT(0x%08xu); goto L_%08x;' % (t, t)
            return 'goto L_%08x;' % t
        return 'EXIT(0x%08xu);' % t

    out = ['void f_%08x(void)\n{\n    switch (cpu.pc) {\n' % lo]
    for l in entries:
        out.append('    case 0x%08xu: goto L_%08x;\n' % (l, l))
    out.append('    default: return;\n    }\n')

    pc = lo
    seg_start = True
    while pc < hi:
        if pc in lset or seg_start:
            n = seg_len(pc)
            out.append('L_%08x: cpu.cycles += %d;\n' % (pc, n) if n else 'L_%08x:\n' % pc)
            seg_start = False
        op = img.word(pc)
        kind, tgt, link = flows[pc]
        if pc in bad:
            out.append('    EXIT(0x%08xu);\n' % pc)
            pc += 4
            seg_start = True
            continue
        if kind is None:
            out.append('    %s\n' % gen_op(pc, op, False))
            pc += 4
            continue

        slot_pc = pc + 4
        slot = gen_op(slot_pc, img.word(slot_pc), True)
        P8 = '0x%08xu' % (pc + 8)
        o = op >> 26
        rs, rd = (op >> 21) & 31, (op >> 11) & 31
        if kind == 'eret':
            out.append('    %s\n' % gen_op(pc, op, False))
            out.append('    EXIT(0x%08xu);\n' % (pc + 4))   # not reached: ERET always returns
            pc += 4
            seg_start = True
            continue
        if kind in ('jr', 'jalr'):
            s = '    { uint32_t _t = (uint32_t)%s; ' % reg(rs)
            if kind == 'jalr' and rd:
                s += '%s = SX32(%s); ' % (reg(rd), P8)
            s += '%s EXIT(_t); }\n' % slot
            out.append(s)
        elif kind == 'jal':
            out.append('    R[31] = SX32(%s); %s EXIT(0x%08xu);\n' % (P8, slot, tgt))
        elif kind == 'j':
            out.append('    { %s %s }\n' % (slot, jump(tgt, pc)))
        elif kind == 'b':
            linkc = 'R[31] = SX32(%s); ' % P8 if link else ''
            out.append('    { bool _c = %s; %s%s if (_c) { %s } }\n' % (gen_cond(op), linkc, slot, jump(tgt, pc)))
        elif kind == 'bl':
            linkc = 'R[31] = SX32(%s); ' % P8 if link else ''
            out.append('    { bool _c = %s; %sif (_c) { %s %s } else cpu.cycles--; }\n' % (gen_cond(op), linkc, slot, jump(tgt, pc)))
        # fall-through (not taken) continues after the delay slot
        nxt = pc + 8
        if kind in ('b', 'bl'):
            if slot_pc in lset and nxt < hi:
                out.append('    goto L_%08x;\n' % nxt)
            elif nxt >= hi:
                out.append('    EXIT(0x%08xu);\n' % nxt)
        pc = slot_pc if slot_pc in lset else nxt
        seg_start = True
    out.append('    EXIT(0x%08xu);\n}\n\n' % hi)
    return ''.join(out), entries


if __name__ == '__main__':
    main()
