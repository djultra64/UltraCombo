// ADSP-2105 interpreter.
// Instruction semantics follow the ADSP-2100 Family User's Manual; edge cases
// (rounding, shifter NORM/EXP, SPORT autobuffer setup) were checked against
// MAME's ADSP-21xx core (BSD-3-Clause, Aaron Giles) so the DCS firmware
// behaves the same way it does there.
#include "adsp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ASTAT bits
#define AZ 0x01
#define AN 0x02
#define AV 0x04
#define AC 0x08
#define AS 0x10
#define AQ 0x20
#define MV 0x40
#define SS 0x80

// MSTAT bits
#define M_BANK  0x01
#define M_BREV  0x02
#define M_STKV  0x04
#define M_SAT   0x08
#define M_INT   0x10
#define M_TIMER 0x20
#define M_GO    0x40

// memory-mapped control registers (offset from 0x3fe0)
enum {
    CR_S1_AUTOBUF = 0x0f, CR_S1_RFSDIV, CR_S1_SCLKDIV, CR_S1_CONTROL,
    CR_TSCALE = 0x1b, CR_TCOUNT, CR_TPERIOD, CR_WAIT, CR_SYSCTL,
};

typedef struct {
    uint16_t ax0, ax1, ay0, ay1, ar, af;
    uint16_t mx0, mx1, my0, my1, mf;
    uint16_t si;
    int8_t   se;
    int8_t   sb;
    uint32_t sr;
    int64_t  mr;      // 40-bit accumulator, kept sign-extended
} regset_t;

static struct {
    adsp_hooks_t h;
    regset_t r, alt;

    uint16_t i[8], l[8], base[8];
    int16_t  m[8];

    uint16_t pc;
    uint16_t pc_stack[16]; int pc_sp;
    uint16_t cntr, cntr_stack[4]; int cntr_sp;
    uint32_t loop_stack[4]; int loop_sp;
    uint16_t loop_end; int loop_cond;
    uint16_t stat_stack[4][3]; int stat_sp;

    uint8_t astat, mstat, sstat, imask, icntl;
    uint16_t ifc;
    uint8_t px;
    bool idle;
    bool polling;             // just looped in a recognised wait-for-autobuffer loop

    uint8_t irq_state[6], irq_latch[6];

    uint32_t ipm[0x400];      // internal program RAM
    uint16_t idm[0x200];      // internal data RAM at 0x3800
    uint16_t ctrl[0x20];      // 0x3fe0-0x3fff

    // timer
    int tscale_cnt;
    // SPORT1 autobuffer transmit
    bool tx_on;
    int tx_ireg, tx_mreg;
    uint16_t tx_base, tx_size;
    int tx_period, tx_cnt;
} a;

static const uint16_t *bitrev;  // unused: bit-reverse mode isn't used by DCS

/* ---------------- memory ---------------- */

static uint16_t dm_read(uint16_t addr)
{
    addr &= 0x3fff;
    if (addr >= 0x3800 && addr < 0x3a00) return a.idm[addr - 0x3800];
    if (addr >= 0x3fe0) return a.ctrl[addr - 0x3fe0];
    return a.h.dm_read(addr);
}

static void ctrl_write(int reg, uint16_t v);

static void dm_write(uint16_t addr, uint16_t v)
{
    addr &= 0x3fff;
    if (addr >= 0x3800 && addr < 0x3a00) { a.idm[addr - 0x3800] = v; return; }
    if (addr >= 0x3fe0) { ctrl_write(addr - 0x3fe0, v); return; }
    a.h.dm_write(addr, v);
}

static uint32_t pm_read(uint16_t addr)
{
    addr &= 0x3fff;
    if (addr < 0x400) return a.ipm[addr];
    return a.h.pm_read(addr) & 0xffffff;
}

static void pm_write(uint16_t addr, uint32_t v)
{
    addr &= 0x3fff;
    if (addr < 0x400) { a.ipm[addr] = v & 0xffffff; return; }
    a.h.pm_write(addr, v & 0xffffff);
}

/* ---------------- stacks ---------------- */

static void pc_push(uint16_t v)
{
    if (a.pc_sp < 16) { a.pc_stack[a.pc_sp++] = v; a.sstat &= ~0x01; }
    else a.sstat |= 0x02;
}
static uint16_t pc_pop(void)
{
    if (a.pc_sp > 0 && --a.pc_sp == 0) a.sstat |= 0x01;
    return a.pc_stack[a.pc_sp];
}
static uint16_t pc_top(void) { return a.pc_stack[a.pc_sp ? a.pc_sp - 1 : 0]; }

static void cntr_push(void)
{
    if (a.cntr_sp < 4) { a.cntr_stack[a.cntr_sp++] = a.cntr; a.sstat &= ~0x04; }
    else a.sstat |= 0x08;
}
static void cntr_pop(void)
{
    if (a.cntr_sp > 0 && --a.cntr_sp == 0) a.sstat |= 0x04;
    a.cntr = a.cntr_stack[a.cntr_sp];
}

static void loop_push(uint32_t v)
{
    if (a.loop_sp < 4) {
        a.loop_stack[a.loop_sp++] = v;
        a.loop_end = (uint16_t)(v >> 4);
        a.loop_cond = v & 15;
        a.sstat &= ~0x40;
    } else a.sstat |= 0x80;
}
static void loop_pop(void)
{
    if (a.loop_sp == 0) return;
    if (--a.loop_sp == 0) { a.loop_end = 0xffff; a.loop_cond = 0; a.sstat |= 0x40; }
    else {
        a.loop_end = (uint16_t)(a.loop_stack[a.loop_sp - 1] >> 4);
        a.loop_cond = a.loop_stack[a.loop_sp - 1] & 15;
    }
}

static void set_mstat(uint8_t v);

static void stat_push(void)
{
    if (a.stat_sp < 4) {
        a.stat_stack[a.stat_sp][0] = a.mstat;
        a.stat_stack[a.stat_sp][1] = a.imask;
        a.stat_stack[a.stat_sp][2] = a.astat;
        a.stat_sp++;
        a.sstat &= ~0x10;
    } else a.sstat |= 0x20;
}
static void stat_pop(void)
{
    if (a.stat_sp > 0 && --a.stat_sp == 0) a.sstat |= 0x10;
    set_mstat((uint8_t)a.stat_stack[a.stat_sp][0]);
    a.imask = (uint8_t)a.stat_stack[a.stat_sp][1];
    a.astat = (uint8_t)a.stat_stack[a.stat_sp][2];
}

/* ---------------- status ---------------- */

static void set_mstat(uint8_t v)
{
    v &= 0x7f;
    if ((v ^ a.mstat) & M_BANK) { regset_t t = a.r; a.r = a.alt; a.alt = t; }
    if ((v ^ a.mstat) & M_TIMER) a.tscale_cnt = 0;
    a.mstat = v;
}

static inline uint8_t clear_mask(void)
{
    return (a.mstat & M_STKV) ? (uint8_t)~(AC | AN | AZ) : (uint8_t)~(AC | AV | AN | AZ);
}

static bool cond(int c)
{
    uint8_t s = a.astat;
    bool z = s & AZ, n = s & AN, v = s & AV, cy = s & AC;
    switch (c) {
    case 0: return z;
    case 1: return !z;
    case 2: return !((n ^ v) | z);
    case 3: return (n ^ v) | z;
    case 4: return n ^ v;
    case 5: return !(n ^ v);
    case 6: return v;
    case 7: return !v;
    case 8: return cy;
    case 9: return !cy;
    case 10: return s & AS;
    case 11: return !(s & AS);
    case 12: return s & MV;
    case 13: return !(s & MV);
    case 14: // NOT CE: decrement the loop counter, pop it when it expires
        a.cntr--;
        if ((int16_t)a.cntr > 0) return true;
        cntr_pop();
        return false;
    default: return true;
    }
}

/* ---------------- registers ---------------- */

static inline uint16_t mr0(void) { return (uint16_t)a.r.mr; }
static inline uint16_t mr1(void) { return (uint16_t)(a.r.mr >> 16); }
static inline uint16_t mr2(void) { return (uint16_t)(int16_t)(int8_t)(a.r.mr >> 32); }

static inline void set_mr0(uint16_t v) { a.r.mr = (a.r.mr & ~0xffffLL) | v; }
static inline void set_mr1(uint16_t v)
{
    // writing MR1 sign-extends into MR2
    a.r.mr = (int64_t)(int16_t)v * 65536 | (a.r.mr & 0xffff);
}
static inline void set_mr2(uint16_t v)
{
    a.r.mr = (int64_t)(int8_t)v * 4294967296LL | (a.r.mr & 0xffffffffLL);
}

static void update_l(int n)
{
    uint16_t l = a.l[n] & 0x3fff;
    uint16_t mask;
    if (l <= 1) mask = 0x3fff;
    else {
        int bits = 32 - __builtin_clz((uint32_t)(l - 1)); // smallest power of two >= l
        mask = (uint16_t)(0x3fff & ~((1u << bits) - 1));
    }
    a.base[n] = a.i[n] & mask;
}

static uint16_t lmask(int n)
{
    uint16_t l = a.l[n] & 0x3fff;
    if (l <= 1) return 0x3fff;
    int bits = 32 - __builtin_clz((uint32_t)(l - 1));
    return (uint16_t)(0x3fff & ~((1u << bits) - 1));
}

static void set_i(int n, uint16_t v) { a.i[n] = v & 0x3fff; a.base[n] = a.i[n] & lmask(n); }
static void set_l(int n, uint16_t v) { a.l[n] = v & 0x3fff; update_l(n); }

static uint16_t rd0(int r)
{
    switch (r) {
    case 0: return a.r.ax0;  case 1: return a.r.ax1;
    case 2: return a.r.mx0;  case 3: return a.r.mx1;
    case 4: return a.r.ay0;  case 5: return a.r.ay1;
    case 6: return a.r.my0;  case 7: return a.r.my1;
    case 8: return a.r.si;   case 9: return (uint16_t)(int16_t)a.r.se;
    case 10: return a.r.ar;  case 11: return mr0();
    case 12: return mr1();   case 13: return mr2();
    case 14: return (uint16_t)a.r.sr;
    default: return (uint16_t)(a.r.sr >> 16);
    }
}

static void wr0(int r, uint16_t v)
{
    switch (r) {
    case 0: a.r.ax0 = v; break;  case 1: a.r.ax1 = v; break;
    case 2: a.r.mx0 = v; break;  case 3: a.r.mx1 = v; break;
    case 4: a.r.ay0 = v; break;  case 5: a.r.ay1 = v; break;
    case 6: a.r.my0 = v; break;  case 7: a.r.my1 = v; break;
    case 8: a.r.si = v; break;   case 9: a.r.se = (int8_t)v; break;
    case 10: a.r.ar = v; break;  case 11: set_mr0(v); break;
    case 12: set_mr1(v); break;  case 13: set_mr2(v); break;
    case 14: a.r.sr = (a.r.sr & 0xffff0000u) | v; break;
    default: a.r.sr = (a.r.sr & 0xffffu) | ((uint32_t)v << 16); break;
    }
}

static uint16_t rd_dag(int base, int r)
{
    int n = base + (r & 3);
    switch (r >> 2) {
    case 0: return a.i[n];
    case 1: return (uint16_t)a.m[n];
    default: return a.l[n];
    }
}

static void wr_dag(int base, int r, uint16_t v)
{
    int n = base + (r & 3);
    switch (r >> 2) {
    case 0: set_i(n, v); break;
    case 1: a.m[n] = (int16_t)(v << 2) >> 2; break;   // 14-bit signed
    case 2: set_l(n, v); break;
    default: break;
    }
}

static void tx1_write(uint16_t v);

static uint16_t rd3(int r)
{
    switch (r) {
    case 0: return a.astat;
    case 1: return a.mstat;
    case 2: return a.sstat;
    case 3: return a.imask;
    case 4: return a.icntl;
    case 5: return a.cntr;
    case 6: return (uint16_t)(int16_t)a.r.sb;
    case 7: return a.px;
    case 15: return pc_pop();
    default: return 0;
    }
}

static void wr3(int r, uint16_t v)
{
    switch (r) {
    case 0: a.astat = (uint8_t)v; break;
    case 1: set_mstat((uint8_t)v); break;
    case 3: a.imask = v & 0x3f; break;
    case 4: a.icntl = v & 0x1f; break;
    case 5: cntr_push(); a.cntr = v & 0x3fff; break;
    case 6: a.r.sb = (int8_t)((int8_t)(v << 3) >> 3); break;   // 5-bit signed
    case 7: a.px = (uint8_t)v; break;
    case 11: tx1_write(v); break;
    case 12:
        a.ifc = v;
        // clear bits 1..5, force bits 7..11 (IRQ0, IRQ1, SPORT0 RX, SPORT0 TX, IRQ2)
        if (v & 0x0002) a.irq_latch[0] = 0;
        if (v & 0x0004) a.irq_latch[1] = 0;
        if (v & 0x0008) a.irq_latch[3] = 0;
        if (v & 0x0010) a.irq_latch[4] = 0;
        if (v & 0x0020) a.irq_latch[2] = 0;
        if (v & 0x0080) a.irq_latch[0] = 1;
        if (v & 0x0100) a.irq_latch[1] = 1;
        if (v & 0x0200) a.irq_latch[3] = 1;
        if (v & 0x0400) a.irq_latch[4] = 1;
        if (v & 0x0800) a.irq_latch[2] = 1;
        break;
    case 13: a.cntr = v & 0x3fff; break;
    case 15: pc_push(v & 0x3fff); break;
    default: break;
    }
}

static uint16_t rdg(int g, int r)
{
    switch (g) {
    case 0: return rd0(r);
    case 1: return rd_dag(0, r);
    case 2: return rd_dag(4, r);
    default: return rd3(r);
    }
}

static void wrg(int g, int r, uint16_t v)
{
    switch (g) {
    case 0: wr0(r, v); break;
    case 1: wr_dag(0, r, v); break;
    case 2: wr_dag(4, r, v); break;
    default: wr3(r, v); break;
    }
}

/* ---------------- DAGs ---------------- */

static inline void post_modify(int ir, int mr)
{
    uint16_t base = a.base[ir], l = a.l[ir];
    uint16_t i = (uint16_t)((a.i[ir] + a.m[mr]) & 0x3fff);
    if (l) {
        if (i < base) i += l;
        else if (i >= base + l) i -= l;
    }
    a.i[ir] = i;
}

static uint16_t bit_reverse14(uint16_t v)
{
    uint16_t r = 0;
    for (int b = 0; b < 14; b++) if (v & (1 << b)) r |= 1 << (13 - b);
    return r;
}

static uint16_t dag1_read(uint32_t op)
{
    int ir = (op >> 2) & 3, mr = op & 3;
    uint16_t addr = (a.mstat & M_BREV) ? bit_reverse14(a.i[ir]) : a.i[ir];
    uint16_t v = dm_read(addr);
    post_modify(ir, mr);
    return v;
}
static void dag1_write(uint32_t op, uint16_t v)
{
    int ir = (op >> 2) & 3, mr = op & 3;
    uint16_t addr = (a.mstat & M_BREV) ? bit_reverse14(a.i[ir]) : a.i[ir];
    dm_write(addr, v);
    post_modify(ir, mr);
}
static uint16_t dag2_read(uint32_t op)
{
    int ir = 4 + ((op >> 2) & 3), mr = 4 + (op & 3);
    uint16_t v = dm_read(a.i[ir]);
    post_modify(ir, mr);
    return v;
}
static void dag2_write(uint32_t op, uint16_t v)
{
    int ir = 4 + ((op >> 2) & 3), mr = 4 + (op & 3);
    dm_write(a.i[ir], v);
    post_modify(ir, mr);
}
static uint16_t pm_dag2_read(uint32_t op)
{
    int ir = 4 + ((op >> 2) & 3), mr = 4 + (op & 3);
    uint32_t w = pm_read(a.i[ir]);
    post_modify(ir, mr);
    a.px = (uint8_t)w;
    return (uint16_t)(w >> 8);
}
static void pm_dag2_write(uint32_t op, uint16_t v)
{
    int ir = 4 + ((op >> 2) & 3), mr = 4 + (op & 3);
    pm_write(a.i[ir], ((uint32_t)v << 8) | a.px);
    post_modify(ir, mr);
}

/* ---------------- ALU ---------------- */

static uint16_t alu_x(int x)
{
    switch (x) {
    case 0: return a.r.ax0; case 1: return a.r.ax1; case 2: return a.r.ar;
    case 3: return mr0(); case 4: return mr1(); case 5: return mr2();
    case 6: return (uint16_t)a.r.sr; default: return (uint16_t)(a.r.sr >> 16);
    }
}
static uint16_t alu_y(int y)
{
    switch (y) { case 0: return a.r.ay0; case 1: return a.r.ay1; case 2: return a.r.af; default: return 0; }
}

static inline void flags_nz(int32_t r)
{
    a.astat &= clear_mask();
    if ((r & 0xffff) == 0) a.astat |= AZ;
    if (r & 0x8000) a.astat |= AN;
}
static inline void flags_v(int32_t s, int32_t d, int32_t r)
{
    a.astat |= ((s ^ d ^ r ^ (r >> 1)) >> 13) & AV;
}

// Returns the 16-bit result and updates ASTAT.
static uint16_t alu(uint32_t op, bool saturate)
{
    int32_t x = alu_x((op >> 8) & 7), y = alu_y((op >> 11) & 3);
    int32_t c = (a.astat & AC) ? 1 : 0;
    int32_t r;
    switch ((op >> 13) & 15) {
    case 0x0: r = y; flags_nz(r); break;
    case 0x1: r = y + 1; flags_nz(r); if (y == 0x7fff) a.astat |= AV; else if (y == 0xffff) a.astat |= AC; break;
    case 0x2: y += c; r = x + y; flags_nz(r); flags_v(x, y, r); a.astat |= (r >> 13) & AC; break;
    case 0x3: r = x + y; flags_nz(r); flags_v(x, y, r); a.astat |= (r >> 13) & AC; break;
    case 0x4: r = y ^ 0xffff; flags_nz(r); break;
    case 0x5: r = -y; flags_nz(r); if (y == 0x8000) a.astat |= AV; if (y == 0) a.astat |= AC; break;
    case 0x6: r = x - y + c - 1; flags_nz(r); flags_v(x, y, r); a.astat |= (~r >> 13) & AC; break;
    case 0x7: r = x - y; flags_nz(r); flags_v(x, y, r); a.astat |= (~r >> 13) & AC; break;
    case 0x8: r = y - 1; flags_nz(r); if (y == 0x8000) a.astat |= AV; else if (y == 0) a.astat |= AC; break;
    case 0x9: r = y - x; flags_nz(r); flags_v(y, x, r); a.astat |= (~r >> 13) & AC; break;
    case 0xa: r = y - x + c - 1; flags_nz(r); flags_v(y, x, r); a.astat |= (~r >> 13) & AC; break;
    case 0xb: r = x ^ 0xffff; flags_nz(r); break;
    case 0xc: r = x & y; flags_nz(r); break;
    case 0xd: r = x | y; flags_nz(r); break;
    case 0xe: r = x ^ y; flags_nz(r); break;
    default:  // ABS
        r = (x & 0x8000) ? -x : x;
        a.astat &= clear_mask();
        a.astat &= ~AS;
        if (x == 0) a.astat |= AZ;
        if (x == 0x8000) a.astat |= AN | AV;
        if (x & 0x8000) a.astat |= AS;
        break;
    }
    if (saturate && (a.mstat & M_SAT) && (a.astat & AV)) r = (a.astat & AC) ? -32768 : 32767;
    return (uint16_t)r;
}

static void alu_ar(uint32_t op) { a.r.ar = alu(op, true); }
static void alu_af(uint32_t op) { a.r.af = alu(op, false); }

/* ---------------- MAC ---------------- */

static uint16_t mac_x(int x)
{
    switch (x) {
    case 0: return a.r.mx0; case 1: return a.r.mx1; case 2: return a.r.ar;
    case 3: return mr0(); case 4: return mr1(); case 5: return mr2();
    case 6: return (uint16_t)a.r.sr; default: return (uint16_t)(a.r.sr >> 16);
    }
}
static uint16_t mac_y(int y)
{
    switch (y) { case 0: return a.r.my0; case 1: return a.r.my1; case 2: return a.r.mf; default: return 0; }
}

// Computes the MAC result; returns false for the no-op encoding.
static bool mac(uint32_t op, int64_t *out)
{
    int shift = (a.mstat & M_INT) ? 0 : 1;
    int xs = (op >> 8) & 7, ys = (op >> 11) & 3;
    int f = (op >> 13) & 15;
    if (f == 0) return false;
    // operand signedness: bit0 of the low two bits = y unsigned, bit1 = x unsigned (for SS/SU/US/UU forms)
    int32_t x, y;
    int form = (f <= 3) ? 0 : (f & 3);   // RND forms use signed*signed
    x = (form & 2) ? (int32_t)mac_x(xs) : (int32_t)(int16_t)mac_x(xs);
    y = (form & 1) ? (int32_t)mac_y(ys) : (int32_t)(int16_t)mac_y(ys);
    int32_t prod = (int32_t)((uint32_t)(x * y) << shift);
    int64_t res;
    switch (f) {
    case 0x1: res = prod; break;
    case 0x2: res = a.r.mr + prod; break;
    case 0x3: res = a.r.mr - prod; break;
    case 0x4: case 0x5: case 0x6: case 0x7: res = prod; break;
    case 0x8: case 0x9: case 0xa: case 0xb: res = a.r.mr + prod; break;
    default: res = a.r.mr - prod; break;
    }
    if (f <= 3) { // RND: round to nearest, ties to even on the product's low half
        res += 0x8000;
        if ((prod & 0xffff) == 0x8000) res &= ~(int64_t)0x10000;
    }
    *out = res;
    return true;
}

static void mac_mr(uint32_t op)
{
    int64_t res;
    if (!mac(op, &res)) return;
    uint32_t top = (uint32_t)(res >> 31) & 0x1ff;
    a.astat &= ~MV;
    if (top != 0 && top != 0x1ff) a.astat |= MV;
    // keep MR as a sign-extended 40-bit quantity
    a.r.mr = (int64_t)((uint64_t)res << 24) >> 24;
}

static void mac_mf(uint32_t op)
{
    int64_t res;
    if (!mac(op, &res)) return;
    a.r.mf = (uint16_t)((uint32_t)res >> 16);
}

/* ---------------- shifter ---------------- */

static uint16_t sh_x(int x)
{
    switch (x) {
    case 0: case 1: return a.r.si; case 2: return a.r.ar;
    case 3: return mr0(); case 4: return mr1(); case 5: return mr2();
    case 6: return (uint16_t)a.r.sr; default: return (uint16_t)(a.r.sr >> 16);
    }
}

static int clz32(uint32_t v) { return v ? __builtin_clz(v) : 32; }

static void shift(uint32_t op, int sc)
{
    int f = (op >> 11) & 15;
    uint16_t xu = sh_x((op >> 8) & 7);
    int32_t xs = (int16_t)xu;
    bool hi = !(f & 2);
    bool or_ = f & 1;
    uint32_t res = 0;

    switch (f >> 2) {
    case 0: { // LSHIFT
        uint32_t v = hi ? (uint32_t)xu << 16 : xu;
        if (sc > 0) res = sc < 32 ? v << sc : 0;
        else res = sc > -32 ? v >> -sc : 0;
        break;
    }
    case 1: { // ASHIFT
        int32_t v = hi ? (int32_t)((uint32_t)xs << 16) : xs;
        if (sc > 0) res = sc < 32 ? (uint32_t)v << sc : 0;
        else res = (uint32_t)(sc > -32 ? v >> -sc : v >> 31);
        break;
    }
    case 2: // NORM
        if (hi) {
            int32_t v = (int32_t)((uint32_t)xs << 16);
            if (sc > 0) {
                v = (int32_t)(((uint32_t)v >> 1) | ((uint32_t)(a.astat & AC) << 28));
                res = (uint32_t)(v >> (sc - 1));
            } else res = sc > -32 ? (uint32_t)v << -sc : 0;
        } else {
            uint32_t v = xu;
            if (sc > 0) res = sc < 32 ? v >> sc : 0;
            else res = sc > -32 ? v << -sc : 0;
        }
        break;
    default: // EXP / EXPADJ (immediate form never reaches here)
        switch (f & 3) {
        case 0: // EXP (HI)
            if (xs < 0) { a.astat |= SS; a.r.se = (int8_t)-(clz32(~(uint32_t)xs) - 17); }
            else { a.astat &= ~SS; a.r.se = (int8_t)-(clz32((uint32_t)xs) - 17); }
            return;
        case 1: // EXP (HIX)
            if (a.astat & AV) {
                a.r.se = 1;
                if (xs < 0) a.astat &= ~SS; else a.astat |= SS;
            } else {
                if (xs < 0) { a.astat |= SS; a.r.se = (int8_t)-(clz32(~(uint32_t)xs) - 17); }
                else { a.astat &= ~SS; a.r.se = (int8_t)-(clz32((uint32_t)xs) - 17); }
            }
            return;
        case 2: // EXP (LO)
            if (a.r.se == -15) {
                uint32_t v = (uint32_t)((a.astat & SS) ? ~xs : xs) & 0xffff;
                a.r.se = (int8_t)-(clz32(v) - 1);
            }
            return;
        default: { // EXPADJ
            int r = clz32((uint32_t)(xs < 0 ? ~xs : xs)) - 17;
            if (r < -a.r.sb) a.r.sb = (int8_t)-r;
            return;
        }
        }
    }
    if (or_) a.r.sr |= res; else a.r.sr = res;
}

static void shift_op(uint32_t op) { shift(op, a.r.se); }
static void shift_imm(uint32_t op)
{
    if (((op >> 11) & 15) >= 12) return;
    shift(op, (int8_t)op);
}

/* ---------------- control regs, timer, SPORT1 ---------------- */

static void tx_recompute(void)
{
    if (a.ctrl[CR_S1_CONTROL] & 0x4000)
        a.tx_period = 2 * (a.ctrl[CR_S1_SCLKDIV] + 1) * 16;
    else
        a.tx_period = 320;   // external 31.25 kHz frame clock at 10 MHz
    if (a.h.sample_rate) a.h.sample_rate(10000000.0 / a.tx_period);
}

static void tx1_write(uint16_t v)
{
    if ((a.ctrl[CR_SYSCTL] & 0x0800) && (a.ctrl[CR_S1_AUTOBUF] & 0x0002)) {
        a.tx_ireg = (a.ctrl[CR_S1_AUTOBUF] >> 9) & 7;
        a.tx_mreg = ((a.ctrl[CR_S1_AUTOBUF] >> 7) & 3) | (a.tx_ireg & 4);
        a.tx_size = a.l[a.tx_ireg];
        // The firmware may or may not have already advanced the pointer;
        // align to the buffer start as the DCS buffers are 16-word aligned.
        a.tx_base = a.i[a.tx_ireg] & ~0xf;
        a.i[a.tx_ireg] = a.tx_base;
        a.tx_on = a.tx_size != 0;
        a.tx_cnt = 0;
        tx_recompute();
        return;
    }
    (void)v;
    a.tx_on = false;
}

static void ctrl_write(int reg, uint16_t v)
{
    a.ctrl[reg] = v;
    switch (reg) {
    case CR_SYSCTL:
        if (v & 0x0200) {
            if (a.h.sysctl_reboot) a.h.sysctl_reboot();
            a.ctrl[CR_SYSCTL] = 0;
            return;
        }
        if (!(v & 0x0800)) a.tx_on = false;
        break;
    case CR_S1_AUTOBUF:
        if (!(v & 0x0002)) a.tx_on = false;
        break;
    case CR_S1_CONTROL: case CR_S1_SCLKDIV:
        if (a.tx_on) tx_recompute();
        break;
    case CR_TSCALE:
        a.tscale_cnt = 0;
        break;
    }
}

static void timer_tick(int cycles)
{
    if (!(a.mstat & M_TIMER)) return;
    int scale = (a.ctrl[CR_TSCALE] & 0xff) + 1;
    a.tscale_cnt += cycles;
    while (a.tscale_cnt >= scale) {
        a.tscale_cnt -= scale;
        if (a.ctrl[CR_TCOUNT] == 0) {
            a.ctrl[CR_TCOUNT] = a.ctrl[CR_TPERIOD];
            a.irq_latch[ADSP_TIMER] = 1;
        } else a.ctrl[CR_TCOUNT]--;
    }
}

static void tx_tick(int cycles)
{
    if (!a.tx_on) return;
    a.tx_cnt += cycles;
    while (a.tx_cnt >= a.tx_period) {
        a.tx_cnt -= a.tx_period;
        uint16_t s = dm_read(a.i[a.tx_ireg]);
        if (a.h.sample_out) a.h.sample_out((int16_t)s);
        uint16_t i = (uint16_t)(a.i[a.tx_ireg] + a.m[a.tx_mreg]);
        if (i >= a.tx_base + a.tx_size) {
            i = a.tx_base;
            adsp_pulse_irq(ADSP_IRQ1);
        }
        a.i[a.tx_ireg] = i & 0x3fff;
    }
}

/* ---------------- interrupts ---------------- */

static bool take_irq(int line, int index)
{
    if (!(a.imask & (0x20 >> index))) return false;
    a.irq_latch[line] = 0;
    pc_push(a.pc);
    stat_push();
    a.pc = (uint16_t)(0x04 + index * 4);
    a.idle = a.polling = false;
    if (a.icntl & 0x10) a.imask &= ~(0x3f >> index);
    else a.imask &= ~0x3f;
    return true;
}

static void check_irqs(void)
{
    bool c;
    c = (a.icntl & 4) ? a.irq_latch[ADSP_IRQ2] : a.irq_state[ADSP_IRQ2];
    if (c && take_irq(ADSP_IRQ2, 0)) return;
    if (a.irq_latch[4] && take_irq(4, 1)) return;
    if (a.irq_latch[3] && take_irq(3, 2)) return;
    c = (a.icntl & 2) ? a.irq_latch[ADSP_IRQ1] : a.irq_state[ADSP_IRQ1];
    if (c && take_irq(ADSP_IRQ1, 3)) return;
    c = (a.icntl & 1) ? a.irq_latch[ADSP_IRQ0] : a.irq_state[ADSP_IRQ0];
    if (c && take_irq(ADSP_IRQ0, 4)) return;
    if (a.irq_latch[ADSP_TIMER] && take_irq(ADSP_TIMER, 5)) return;
}

void adsp_set_irq(int line, bool state)
{
    if (state && !a.irq_state[line]) a.irq_latch[line] = 1;
    a.irq_state[line] = state;
}

void adsp_pulse_irq(int line)
{
    adsp_set_irq(line, true);
    adsp_set_irq(line, false);
}

/* ---------------- execution ---------------- */

// "top: Dreg = Ireg; AF|AR = Dreg op AYn; IF cond JUMP top" only rereads an
// index register, which nothing but the autobuffer (or an interrupt) changes,
// and every iteration leaves the same state behind. Once it has looped, the
// DSP may skip ahead to the next transmitted sample.
static bool is_poll_loop(uint16_t top, uint16_t jump_at)
{
    static uint16_t yes[8], no[32];
    static int nyes, nno;
    for (int i = 0; i < nyes; i++) if (yes[i] == top) return true;
    for (int i = 0; i < nno && i < 32; i++) if (no[i] == top) return false;

    static const uint8_t xop_dreg[8] = { 0, 1, 10, 11, 12, 13, 14, 15 };
    uint32_t mv = pm_read(top), alu = pm_read((uint16_t)(top + 1));
    int amf = (alu >> 13) & 31, src_grp = (mv >> 8) & 3;
    bool match = jump_at == top + 2 &&
        (mv >> 16) == 0x0d && ((mv >> 10) & 3) == 0 &&                 // Dreg = ...
        (src_grp == 1 || src_grp == 2) && (mv & 15) < 4 &&              // ... I0-I7
        ((alu >> 16) == 0x22 || (alu >> 16) == 0x23 || (alu >> 16) == 0x26 || (alu >> 16) == 0x27) &&
        (alu & 15) == 15 && amf >= 0x10 && amf != 0x12 && amf != 0x16 && amf != 0x1a &&  // ALU, no carry in
        ((alu >> 11) & 3) != 2 &&                                       // Y is not AF
        xop_dreg[(alu >> 8) & 7] == ((mv >> 4) & 15) &&                 // X is the register just loaded
        !(a.loop_sp && a.loop_end >= top && a.loop_end <= jump_at);
    if (match) { if (nyes < 8) yes[nyes++] = top; }
    else no[nno++ % 32] = top;
    return match;
}

static void exec(uint32_t op)
{
    uint16_t t;
    switch (op >> 16) {
    case 0x00: break;
    case 0x02:
        if (op & 0x8000) a.idle = true;   // IDLE
        break;                            // flag outputs are not wired on DCS
    case 0x03: break;                     // jump/call on FLAG_IN: FI is never asserted
    case 0x04:
        if (op & 0x10) pc_pop();
        if (op & 0x08) loop_pop();
        if (op & 0x04) cntr_pop();
        if (op & 0x02) { if (op & 1) stat_pop(); else stat_push(); }
        break;
    case 0x05:
        if (a.astat & MV) {
            if (a.r.mr & 0x8000000000LL) a.r.mr = -0x80000000LL;      // 0xff 8000 0000
            else a.r.mr = 0x7fffffffLL;                                  // 0x00 7fff ffff
        }
        break;
    case 0x06: { // DIVS
        uint16_t y = alu_y((op >> 11) & 3), x = alu_x((op >> 8) & 7);
        uint16_t tq = x ^ y;
        a.astat = (a.astat & ~AQ) | ((tq >> 10) & AQ);
        a.r.af = (uint16_t)((y << 1) | (a.r.ay0 >> 15));
        a.r.ay0 = (uint16_t)((a.r.ay0 << 1) | (tq >> 15));
        break;
    }
    case 0x07: { // DIVQ
        uint16_t x = alu_x((op >> 8) & 7);
        uint32_t res = (a.astat & AQ) ? (uint32_t)a.r.af + x : (uint32_t)a.r.af - x;
        uint32_t tq = res ^ x;
        a.astat = (a.astat & ~AQ) | ((tq >> 10) & AQ);
        a.r.af = (uint16_t)((res << 1) | (a.r.ay0 >> 15));
        a.r.ay0 = (uint16_t)((a.r.ay0 << 1) | ((~tq >> 15) & 1));
        break;
    }
    case 0x09: { // MODIFY
        int ir = (op >> 2) & 7;
        post_modify(ir, (ir & 4) | (op & 3));
        break;
    }
    case 0x0a: // RTS / RTI
        if (cond(op & 15)) {
            a.pc = pc_pop();
            if (op & 0x10) stat_pop();
        }
        break;
    case 0x0b: // indirect jump/call
        if (cond(op & 15)) {
            if (op & 0x10) pc_push(a.pc);
            a.pc = a.i[4 + ((op >> 6) & 3)] & 0x3fff;
        }
        break;
    case 0x0c: { // ENA/DIS mode
        uint8_t m = a.mstat;
        if (op & (1 << 3)) m = (op & (1 << 2)) ? (m | M_GO) : (m & ~M_GO);
        if (op & (1 << 13)) m = (op & (1 << 12)) ? (m | M_INT) : (m & ~M_INT);
        if (op & (1 << 15)) m = (op & (1 << 14)) ? (m | M_TIMER) : (m & ~M_TIMER);
        if (op & (1 << 5)) m = (op & (1 << 4)) ? (m | M_BANK) : (m & ~M_BANK);
        if (op & (1 << 7)) m = (op & (1 << 6)) ? (m | M_BREV) : (m & ~M_BREV);
        if (op & (1 << 9)) m = (op & (1 << 8)) ? (m | M_STKV) : (m & ~M_STKV);
        if (op & (1 << 11)) m = (op & (1 << 10)) ? (m | M_SAT) : (m & ~M_SAT);
        set_mstat(m);
        break;
    }
    case 0x0d: // register move
        wrg((op >> 10) & 3, (op >> 4) & 15, rdg((op >> 8) & 3, op & 15));
        break;
    case 0x0e: if (cond(op & 15)) shift_op(op); break;
    case 0x0f: shift_imm(op); break;
    case 0x10: shift_op(op); t = rd0(op & 15); wr0((op >> 4) & 15, t); break;
    case 0x11:
        if (op & 0x8000) { pm_dag2_write(op, rd0((op >> 4) & 15)); shift_op(op); }
        else { shift_op(op); wr0((op >> 4) & 15, pm_dag2_read(op)); }
        break;
    case 0x12:
        if (op & 0x8000) { dag1_write(op, rd0((op >> 4) & 15)); shift_op(op); }
        else { shift_op(op); wr0((op >> 4) & 15, dag1_read(op)); }
        break;
    case 0x13:
        if (op & 0x8000) { dag2_write(op, rd0((op >> 4) & 15)); shift_op(op); }
        else { shift_op(op); wr0((op >> 4) & 15, dag2_read(op)); }
        break;
    case 0x14: case 0x15: case 0x16: case 0x17: // DO UNTIL
        loop_push(op & 0x3ffff);
        pc_push(a.pc);
        break;
    case 0x18: case 0x19: case 0x1a: case 0x1b: // JUMP
        if (cond(op & 15)) {
            uint16_t at = (uint16_t)(a.pc - 1);
            a.pc = (op >> 4) & 0x3fff;
            if (a.pc < at && is_poll_loop(a.pc, at)) a.polling = true;
        }
        break;
    case 0x1c: case 0x1d: case 0x1e: case 0x1f: // CALL
        if (cond(op & 15)) { pc_push(a.pc); a.pc = (op >> 4) & 0x3fff; }
        break;
    case 0x20: case 0x21: if (cond(op & 15)) mac_mr(op); break;
    case 0x22: case 0x23: if (cond(op & 15)) alu_ar(op); break;
    case 0x24: case 0x25: if (cond(op & 15)) mac_mf(op); break;
    case 0x26: case 0x27: if (cond(op & 15)) alu_af(op); break;
    case 0x28: case 0x29: t = rd0(op & 15); mac_mr(op); wr0((op >> 4) & 15, t); break;
    case 0x2a: case 0x2b: t = rd0(op & 15); alu_ar(op); wr0((op >> 4) & 15, t); break;
    case 0x2c: case 0x2d: t = rd0(op & 15); mac_mf(op); wr0((op >> 4) & 15, t); break;
    case 0x2e: case 0x2f: t = rd0(op & 15); alu_af(op); wr0((op >> 4) & 15, t); break;
    case 0x30 ... 0x3f: { // load non-data register, 14-bit immediate
        uint16_t imm = (uint16_t)((int16_t)(((op >> 4) & 0x3fff) << 2) >> 2);
        wrg((op >> 18) & 3, op & 15, imm);
        break;
    }
    case 0x40 ... 0x4f: wr0(op & 15, (uint16_t)(op >> 4)); break;
    case 0x50: case 0x51: mac_mr(op); wr0((op >> 4) & 15, pm_dag2_read(op)); break;
    case 0x52: case 0x53: alu_ar(op); wr0((op >> 4) & 15, pm_dag2_read(op)); break;
    case 0x54: case 0x55: mac_mf(op); wr0((op >> 4) & 15, pm_dag2_read(op)); break;
    case 0x56: case 0x57: alu_af(op); wr0((op >> 4) & 15, pm_dag2_read(op)); break;
    case 0x58: case 0x59: pm_dag2_write(op, rd0((op >> 4) & 15)); mac_mr(op); break;
    case 0x5a: case 0x5b: pm_dag2_write(op, rd0((op >> 4) & 15)); alu_ar(op); break;
    case 0x5c: case 0x5d: pm_dag2_write(op, rd0((op >> 4) & 15)); mac_mf(op); break;
    case 0x5e: case 0x5f: pm_dag2_write(op, rd0((op >> 4) & 15)); alu_af(op); break;
    case 0x60: case 0x61: mac_mr(op); wr0((op >> 4) & 15, dag1_read(op)); break;
    case 0x62: case 0x63: alu_ar(op); wr0((op >> 4) & 15, dag1_read(op)); break;
    case 0x64: case 0x65: mac_mf(op); wr0((op >> 4) & 15, dag1_read(op)); break;
    case 0x66: case 0x67: alu_af(op); wr0((op >> 4) & 15, dag1_read(op)); break;
    case 0x68: case 0x69: dag1_write(op, rd0((op >> 4) & 15)); mac_mr(op); break;
    case 0x6a: case 0x6b: dag1_write(op, rd0((op >> 4) & 15)); alu_ar(op); break;
    case 0x6c: case 0x6d: dag1_write(op, rd0((op >> 4) & 15)); mac_mf(op); break;
    case 0x6e: case 0x6f: dag1_write(op, rd0((op >> 4) & 15)); alu_af(op); break;
    case 0x70: case 0x71: mac_mr(op); wr0((op >> 4) & 15, dag2_read(op)); break;
    case 0x72: case 0x73: alu_ar(op); wr0((op >> 4) & 15, dag2_read(op)); break;
    case 0x74: case 0x75: mac_mf(op); wr0((op >> 4) & 15, dag2_read(op)); break;
    case 0x76: case 0x77: alu_af(op); wr0((op >> 4) & 15, dag2_read(op)); break;
    case 0x78: case 0x79: dag2_write(op, rd0((op >> 4) & 15)); mac_mr(op); break;
    case 0x7a: case 0x7b: dag2_write(op, rd0((op >> 4) & 15)); alu_ar(op); break;
    case 0x7c: case 0x7d: dag2_write(op, rd0((op >> 4) & 15)); mac_mf(op); break;
    case 0x7e: case 0x7f: dag2_write(op, rd0((op >> 4) & 15)); alu_af(op); break;
    case 0x80 ... 0x8f: wrg((op >> 18) & 3, op & 15, dm_read((op >> 4) & 0x3fff)); break;
    case 0x90 ... 0x9f: dm_write((op >> 4) & 0x3fff, rdg((op >> 18) & 3, op & 15)); break;
    case 0xa0 ... 0xaf: dag1_write(op, (uint16_t)(op >> 4)); break;
    case 0xb0 ... 0xbf: dag2_write(op, (uint16_t)(op >> 4)); break;
    default: { // 0xc0-0xff: ALU/MAC with dual memory read
        uint32_t hi = op >> 16;
        if (hi & 2) alu_ar(op); else mac_mr(op);
        uint16_t dv = dag1_read(op);
        uint16_t pv = pm_dag2_read(op >> 4);
        switch ((hi >> 2) & 3) {
        case 0: a.r.ax0 = dv; break; case 1: a.r.ax1 = dv; break;
        case 2: a.r.mx0 = dv; break; default: a.r.mx1 = dv; break;
        }
        switch ((hi >> 4) & 3) {
        case 0: a.r.ay0 = pv; break; case 1: a.r.ay1 = pv; break;
        case 2: a.r.my0 = pv; break; default: a.r.my1 = pv; break;
        }
        break;
    }
    }
}

static inline bool any_irq(void)
{
    uint32_t v = 0;
    for (int i = 0; i < 6; i++) v |= a.irq_state[i] | a.irq_latch[i];
    return v != 0;
}

void adsp_run(int cycles)
{
    while (cycles > 0) {
        if (any_irq()) check_irqs();
        if (a.idle || a.polling) {
            a.polling = false;
            // fast-forward to the next autobuffer sample (or the end of the slice)
            int step = cycles;
            if (a.tx_on) {
                int to_next = a.tx_period - a.tx_cnt;
                if (to_next < step) step = to_next < 1 ? 1 : to_next;
            }
            if ((a.mstat & M_TIMER) && step > 1) step = 1 + (step > 64 ? 63 : step - 1);
            timer_tick(step);
            tx_tick(step);
            cycles -= step;
            continue;
        }

        uint32_t op = pm_read(a.pc);
        if (a.pc != a.loop_end) a.pc++;
        else if (cond(a.loop_cond)) a.pc = pc_top();
        else { loop_pop(); pc_pop(); a.pc++; }
        a.pc &= 0x3fff;

        exec(op);
        if (a.mstat & M_TIMER) timer_tick(1);
        if (a.tx_on && ++a.tx_cnt >= a.tx_period) { a.tx_cnt--; tx_tick(1); }
        cycles--;
    }
}

void adsp_init(const adsp_hooks_t *hooks)
{
    memset(&a, 0, sizeof(a));
    a.h = *hooks;
    (void)bitrev;
}

void adsp_reset(void)
{
    adsp_hooks_t h = a.h;
    uint32_t ipm[0x400];
    memcpy(ipm, a.ipm, sizeof ipm);
    memset(&a, 0, sizeof(a));
    a.h = h;
    memcpy(a.ipm, ipm, sizeof ipm);
    a.pc = 0;
    a.loop_end = 0xffff;
    a.sstat = 0x55;
    for (int n = 0; n < 8; n++) update_l(n);
}

uint32_t *adsp_internal_pm(void) { return a.ipm; }
uint16_t adsp_pc(void) { return a.pc; }
