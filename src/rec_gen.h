// Helpers included by the C files tools/recomp.py generates.
#pragma once
#include "rec.h"

#if defined(__TINYC__)
// TinyCC (used to build the translated code on the player's machine) has
// neither gcc's overflow builtins nor 128-bit integers.
static int rec_add_ov(int32_t a, int32_t b, int32_t *r) { int64_t s = (int64_t)a + b; *r = (int32_t)s; return s != *r; }
static int rec_sub_ov(int32_t a, int32_t b, int32_t *r) { int64_t s = (int64_t)a - b; *r = (int32_t)s; return s != *r; }
#define __builtin_add_overflow(a, b, r) rec_add_ov((a), (b), (r))
#define __builtin_sub_overflow(a, b, r) rec_sub_ov((a), (b), (r))
static void rec_mul64(uint64_t a, uint64_t b, uint64_t *hi, uint64_t *lo)
{
    uint64_t al = (uint32_t)a, ah = a >> 32, bl = (uint32_t)b, bh = b >> 32;
    uint64_t ll = al * bl, lh = al * bh, hl = ah * bl, hh = ah * bh;
    uint64_t mid = (ll >> 32) + (uint32_t)lh + (uint32_t)hl;
    *lo = (mid << 32) | (uint32_t)ll;
    *hi = hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
}
#define REC_LOAD(T, v, p) ((v) = *(const T *)(p))
#define REC_STORE(T, p, v) (*(T *)(p) = (v))
#else
#define REC_LOAD(T, v, p) memcpy(&(v), (p), sizeof(T))
#define REC_STORE(T, p, v) memcpy((p), &(v), sizeof(T))
#endif

#define R cpu.r
#define SX32(x) ((uint64_t)(int64_t)(int32_t)(x))
#define EXIT(t) do { cpu.pc = (t); cpu.npc = (t) + 4; return; } while (0)
// at a backward branch: give the dispatcher a chance to take interrupts / end the slice
#define STOP() (cpu.cycles >= rec_deadline || irq_dirty)

static inline void rec_mult(uint64_t rs, uint64_t rt)
{
    int64_t p = (int64_t)(int32_t)rs * (int32_t)rt;
    cpu.lo = SX32((uint32_t)p); cpu.hi = SX32((uint32_t)(p >> 32));
}
static inline void rec_multu(uint64_t rs, uint64_t rt)
{
    uint64_t p = (uint64_t)(uint32_t)rs * (uint32_t)rt;
    cpu.lo = SX32((uint32_t)p); cpu.hi = SX32((uint32_t)(p >> 32));
}
static inline void rec_div(uint64_t rs, uint64_t rt)
{
    int32_t a = (int32_t)rs, b = (int32_t)rt;
    if (b == 0) { cpu.lo = SX32(a < 0 ? 1 : 0xffffffffu); cpu.hi = SX32(a); }
    else if (a == INT32_MIN && b == -1) { cpu.lo = SX32(a); cpu.hi = 0; }
    else { cpu.lo = SX32(a / b); cpu.hi = SX32(a % b); }
}
static inline void rec_divu(uint64_t rs, uint64_t rt)
{
    uint32_t a = (uint32_t)rs, b = (uint32_t)rt;
    if (b == 0) { cpu.lo = SX32(0xffffffffu); cpu.hi = SX32(a); }
    else { cpu.lo = SX32(a / b); cpu.hi = SX32(a % b); }
}
#if defined(__TINYC__)
static inline void rec_dmultu(uint64_t rs, uint64_t rt) { rec_mul64(rs, rt, &cpu.hi, &cpu.lo); }
static inline void rec_dmult(uint64_t rs, uint64_t rt)
{
    rec_mul64(rs, rt, &cpu.hi, &cpu.lo);                  // signed: correct the high half
    if ((int64_t)rs < 0) cpu.hi -= rt;
    if ((int64_t)rt < 0) cpu.hi -= rs;
}
#else
static inline void rec_dmult(uint64_t rs, uint64_t rt)
{
    __int128 p = (__int128)(int64_t)rs * (int64_t)rt;
    cpu.lo = (uint64_t)p; cpu.hi = (uint64_t)(p >> 64);
}
static inline void rec_dmultu(uint64_t rs, uint64_t rt)
{
    unsigned __int128 p = (unsigned __int128)rs * rt;
    cpu.lo = (uint64_t)p; cpu.hi = (uint64_t)(p >> 64);
}
#endif
static inline void rec_ddiv(uint64_t rs, uint64_t rt)
{
    int64_t a = (int64_t)rs, b = (int64_t)rt;
    if (b == 0) { cpu.lo = a < 0 ? 1 : ~0ull; cpu.hi = a; }
    else if (a == INT64_MIN && b == -1) { cpu.lo = a; cpu.hi = 0; }
    else { cpu.lo = a / b; cpu.hi = a % b; }
}
static inline void rec_ddivu(uint64_t rs, uint64_t rt)
{
    if (rt == 0) { cpu.lo = ~0ull; cpu.hi = rs; }
    else { cpu.lo = rs / rt; cpu.hi = rs % rt; }
}

#if defined(__TINYC__)
// TinyCC never inlines, so the RAM fast path is spelled out here
// (ram0 and ram2 are contiguous in `ram`).
#define REC_PA(a) ((a) & 0x1fffffffu)
#define rec_ld_ptr_(a) (((a) & 0xc0000000u) != 0x80000000u ? rec_tlbc((a), false) :           \
        REC_PA(a) < RAM0_SIZE ? ram + REC_PA(a) :                                               \
        REC_PA(a) - 0x08000000u < RAM2_SIZE ? ram + RAM0_SIZE + (REC_PA(a) - 0x08000000u) : (uint8_t *)0)
#define rec_st_ptr_(a) (((a) & 0xc0000000u) != 0x80000000u ? rec_tlbc((a), true) :            \
        REC_PA(a) < RAM0_SIZE ? ram + REC_PA(a) :                                               \
        REC_PA(a) - rec_code_lo < rec_code_size ? (uint8_t *)0 :                                \
        REC_PA(a) - 0x08000000u < RAM2_SIZE ? ram + RAM0_SIZE + (REC_PA(a) - 0x08000000u) : (uint8_t *)0)
#define ram_touch_(p) (ram_dirty[(size_t)((p) - ram) >> 12] = 1)
#else
#define rec_ld_ptr_ rec_ld_ptr
#define rec_st_ptr_ rec_st_ptr
#define ram_touch_ ram_touch
#endif

// Loads and stores: inline fast path for KSEG0/1 RAM, helper otherwise.
#define LD(T, dst, va, size, PC, DL) do {                                   \
        uint32_t _a = (va); uint8_t *_p = rec_ld_ptr_(_a); T _v;             \
        if (_p && !(_a & (size - 1))) REC_LOAD(T, _v, _p);                    \
        else { _v = (T)rec_load(_a, size, PC, DL); if (cpu_exc) return; }    \
        dst;                                                                 \
    } while (0)
#define ST(T, val, va, size, PC, DL) do {                                   \
        uint32_t _a = (va); uint8_t *_p = rec_st_ptr_(_a); T _v = (T)(val);  \
        if (_p && !(_a & (size - 1))) { REC_STORE(T, _p, _v); ram_touch_(_p); } \
        else { rec_store(_a, (uint64_t)_v, size, PC, DL); if (cpu_exc) return; } \
    } while (0)

// Unaligned LWL/LWR/SWL/SWR/LDL/LDR/SDL/SDR: read (and write back) the
// aligned word containing ea, merging as the interpreter does. Return false
// when an exception was raised.
static inline bool rec_ld_aligned(uint64_t *w, uint32_t a, int size, uint32_t pc, bool dl)
{
    uint8_t *p = rec_ld_ptr(a);
    if (p) { if (size == 8) memcpy(w, p, 8); else { uint32_t v; memcpy(&v, p, 4); *w = v; } return true; }
    *w = rec_load(a, size, pc, dl);
    return !cpu_exc;
}
static inline bool rec_st_aligned(uint64_t w, uint32_t a, int size, uint32_t pc, bool dl)
{
    uint8_t *p = rec_st_ptr(a);
    if (p) { if (size == 8) memcpy(p, &w, 8); else { uint32_t v = (uint32_t)w; memcpy(p, &v, 4); } ram_touch(p); return true; }
    rec_store(a, w, size, pc, dl);
    return !cpu_exc;
}
static inline bool rec_lwl(uint64_t *rt, uint32_t ea, uint32_t pc, bool dl)
{
    uint64_t w; if (!rec_ld_aligned(&w, ea & ~3u, 4, pc, dl)) return false;
    int sh = 8 * (3 - (ea & 3));
    uint32_t keep = sh ? ((1u << sh) - 1) : 0;
    *rt = SX32(((uint32_t)*rt & keep) | ((uint32_t)w << sh));
    return true;
}
static inline bool rec_lwr(uint64_t *rt, uint32_t ea, uint32_t pc, bool dl)
{
    uint64_t w; if (!rec_ld_aligned(&w, ea & ~3u, 4, pc, dl)) return false;
    int sh = 8 * (ea & 3);
    uint32_t keep = sh ? ~(0xffffffffu >> sh) : 0;
    uint32_t v = ((uint32_t)*rt & keep) | ((uint32_t)w >> sh);
    *rt = sh ? ((*rt & 0xffffffff00000000ull) | v) : SX32(v);
    return true;
}
static inline bool rec_ldl(uint64_t *rt, uint32_t ea, uint32_t pc, bool dl)
{
    uint64_t w; if (!rec_ld_aligned(&w, ea & ~7u, 8, pc, dl)) return false;
    int sh = 8 * (7 - (ea & 7));
    uint64_t keep = sh ? ((1ull << sh) - 1) : 0;
    *rt = (*rt & keep) | (w << sh);
    return true;
}
static inline bool rec_ldr(uint64_t *rt, uint32_t ea, uint32_t pc, bool dl)
{
    uint64_t w; if (!rec_ld_aligned(&w, ea & ~7u, 8, pc, dl)) return false;
    int sh = 8 * (ea & 7);
    uint64_t keep = sh ? ~(~0ull >> sh) : 0;
    *rt = (*rt & keep) | (w >> sh);
    return true;
}
static inline bool rec_swl(uint64_t rt, uint32_t ea, uint32_t pc, bool dl)
{
    uint64_t w; if (!rec_ld_aligned(&w, ea & ~3u, 4, pc, dl)) return false;
    int sh = 8 * (3 - (ea & 3));
    uint32_t mask = 0xffffffffu >> sh;
    return rec_st_aligned(((uint32_t)w & ~mask) | ((uint32_t)rt >> sh), ea & ~3u, 4, pc, dl);
}
static inline bool rec_swr(uint64_t rt, uint32_t ea, uint32_t pc, bool dl)
{
    uint64_t w; if (!rec_ld_aligned(&w, ea & ~3u, 4, pc, dl)) return false;
    int sh = 8 * (ea & 3);
    uint32_t mask = 0xffffffffu << sh;
    return rec_st_aligned(((uint32_t)w & ~mask) | ((uint32_t)rt << sh), ea & ~3u, 4, pc, dl);
}
static inline bool rec_sdl(uint64_t rt, uint32_t ea, uint32_t pc, bool dl)
{
    uint64_t w; if (!rec_ld_aligned(&w, ea & ~7u, 8, pc, dl)) return false;
    int sh = 8 * (7 - (ea & 7));
    uint64_t mask = ~0ull >> sh;
    return rec_st_aligned((w & ~mask) | (rt >> sh), ea & ~7u, 8, pc, dl);
}
static inline bool rec_sdr(uint64_t rt, uint32_t ea, uint32_t pc, bool dl)
{
    uint64_t w; if (!rec_ld_aligned(&w, ea & ~7u, 8, pc, dl)) return false;
    int sh = 8 * (ea & 7);
    uint64_t mask = ~0ull << sh;
    return rec_st_aligned((w & ~mask) | (rt << sh), ea & ~7u, 8, pc, dl);
}

// COP0 is usable in kernel mode, at exception level, or with Status.CU0 set.
static inline bool rec_cop0_usable(void)
{
    uint32_t sr = (uint32_t)cpu.cp0[CP0_STATUS];
    return !(sr & SR_KSU) || (sr & (SR_EXL | SR_ERL)) || (sr & 0x10000000u);
}
