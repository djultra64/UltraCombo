// Helpers included by the C files tools/recomp.py generates.
#pragma once
#include "rec.h"

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

// Loads and stores: inline fast path for KSEG0/1 RAM, helper otherwise.
#define LD(T, dst, va, size, PC, DL) do {                                   \
        uint32_t _a = (va); uint8_t *_p = rec_ld_ptr(_a); T _v;              \
        if (_p && !(_a & (size - 1))) memcpy(&_v, _p, size);                 \
        else { _v = (T)rec_load(_a, size, PC, DL); if (cpu_exc) return; }    \
        dst;                                                                 \
    } while (0)
#define ST(T, val, va, size, PC, DL) do {                                   \
        uint32_t _a = (va); uint8_t *_p = rec_st_ptr(_a); T _v = (T)(val);   \
        if (_p && !(_a & (size - 1))) memcpy(_p, &_v, size);                 \
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
    if (p) { if (size == 8) memcpy(p, &w, 8); else { uint32_t v = (uint32_t)w; memcpy(p, &v, 4); } return true; }
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
