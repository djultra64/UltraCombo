// Statically recompiled MIPS code: runtime interface.
//
// tools/recomp.py translates the game's RAM-resident code (from a snapshot)
// into C functions, one per MIPS function, with a switch on cpu.pc at the top
// so any labelled address inside can be entered. cpu_run calls into them
// whenever cpu.pc hits a label and falls back to the interpreter otherwise.
// A translated function is used only while the bytes in RAM still match the
// snapshot it was built from.
#pragma once
#include "cpu.h"
#include "mem.h"
#include <string.h>

typedef void (*rec_fn_t)(void);

typedef struct {
    uint32_t pc;        // labelled entry address
    uint16_t fn;        // index into rec_funcs
} rec_label_t;

typedef struct {
    rec_fn_t fn;
    uint32_t lo, hi;    // MIPS address range the C code was generated from
} rec_func_t;

// Generated tables (build/rec/rec_tab.c, or src/rec_none.c when absent)
extern const rec_func_t  rec_funcs[];
extern const rec_label_t rec_labels[];
extern const uint32_t    rec_nfuncs, rec_nlabels;
extern const uint32_t    rec_image_base, rec_image_words;
extern const uint32_t    rec_image[];

// Physical range covered by the image; any store into it bumps rec_code_gen.
extern uint32_t rec_code_lo, rec_code_size, rec_code_gen;
extern bool     rec_enabled;

void     rec_init(void);
rec_fn_t rec_lookup_slow(uint32_t pc);
extern rec_fn_t *rec_map;          // per-word entry, valid for the current rec_code_gen
extern uint32_t rec_map_gen;

static inline rec_fn_t rec_lookup(uint32_t pc)
{
    uint32_t i = (pc - rec_image_base) >> 2;
    if (i >= rec_image_words || (pc & 3)) return 0;
    if (rec_map_gen != rec_code_gen) return rec_lookup_slow(pc);
    return rec_map[i];
}

// Used by generated code
extern bool     cpu_exc;           // set by a helper when it raised an exception
extern bool     irq_dirty;
extern uint64_t rec_deadline;      // return to the dispatcher at a back-edge past this cycle
uint64_t rec_load(uint32_t va, int size, uint32_t pc, bool delay);
void     rec_store(uint32_t va, uint64_t v, int size, uint32_t pc, bool delay);
bool     rec_op(uint32_t op, uint32_t pc, bool delay);   // interpret one non-branch op

// Cache of TLB-mapped 4 KB pages (cpu.c)
#define TLBC_SIZE 1024
typedef struct {
    uint32_t tag;       // virtual page number + 1, 0 = empty
    uint32_t pa;        // physical page
    bool     dirty;     // writes allowed
    uint8_t *rd, *wr;   // host page for loads / stores, NULL for I/O, ROM or code
} tlbc_t;
extern tlbc_t tlbc[TLBC_SIZE];

static inline uint8_t *rec_tlbc(uint32_t va, bool write)
{
    tlbc_t *e = &tlbc[(va >> 12) & (TLBC_SIZE - 1)];
    if (e->tag != (va >> 12) + 1) return 0;
    uint8_t *p = write ? e->wr : e->rd;
    return p ? p + (va & 0xfff) : 0;
}

// RAM that can be accessed directly, or NULL when the slow path is needed.
static inline uint8_t *rec_ld_ptr(uint32_t va)
{
    if ((va & 0xc0000000u) != 0x80000000u) return rec_tlbc(va, false);
    uint32_t pa = va & 0x1fffffffu;
    if (pa < RAM0_SIZE) return &ram0[pa];
    if (pa - 0x08000000u < RAM2_SIZE) return &ram2[pa - 0x08000000u];
    return 0;
}

// As rec_ld_ptr, but stores into the code image take the slow path so they invalidate it.
static inline uint8_t *rec_st_ptr(uint32_t va)
{
    if ((va & 0xc0000000u) != 0x80000000u) return rec_tlbc(va, true);
    uint32_t pa = va & 0x1fffffffu;
    if (pa < RAM0_SIZE) return &ram0[pa];
    if (pa - rec_code_lo < rec_code_size) return 0;
    if (pa - 0x08000000u < RAM2_SIZE) return &ram2[pa - 0x08000000u];
    return 0;
}
