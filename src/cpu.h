// R4600 (MIPS III, little-endian) CPU state and interpreter interface.
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint64_t mask, hi, lo0, lo1;
} tlb_entry_t;

typedef struct {
    uint64_t r[32];
    uint64_t hi, lo;
    uint32_t pc;          // address of the instruction about to execute
    uint32_t npc;         // address of the one after it
    uint32_t cur_pc;      // address of the instruction currently executing
    bool     in_delay;    // instruction at pc is a branch delay slot
    bool     next_delay;  // set by a branch: instruction at npc is a delay slot

    uint64_t cp0[32];
    uint64_t fpr[32];
    uint32_t fcr31;
    bool     llbit;

    tlb_entry_t tlb[48];

    uint64_t cycles;      // total instructions executed
    uint32_t count_base;  // Count value at count_cyc; Count ticks at half the pipeline clock
    uint64_t count_cyc;
    uint64_t next_compare; // cycle at which Count reaches Compare
    uint32_t ext_irq;     // external interrupt lines (bit n = Int n)
} cpu_t;

enum {
    CP0_INDEX = 0, CP0_RANDOM, CP0_ENTRYLO0, CP0_ENTRYLO1, CP0_CONTEXT,
    CP0_PAGEMASK, CP0_WIRED, CP0_BADVADDR = 8, CP0_COUNT, CP0_ENTRYHI,
    CP0_COMPARE, CP0_STATUS, CP0_CAUSE, CP0_EPC, CP0_PRID, CP0_CONFIG,
    CP0_LLADDR, CP0_WATCHLO, CP0_WATCHHI, CP0_XCONTEXT = 20,
    CP0_ECC = 26, CP0_CACHEERR, CP0_TAGLO, CP0_TAGHI, CP0_ERROREPC,
};

#define SR_IE   0x00000001u
#define SR_EXL  0x00000002u
#define SR_ERL  0x00000004u
#define SR_KSU  0x00000018u
#define SR_BEV  0x00400000u
#define SR_FR   0x04000000u
#define SR_CU1  0x20000000u

enum {
    EXC_INT = 0, EXC_MOD, EXC_TLBL, EXC_TLBS, EXC_ADEL, EXC_ADES, EXC_IBE,
    EXC_DBE, EXC_SYS, EXC_BP, EXC_RI, EXC_CPU, EXC_OV, EXC_TR, EXC_FPE = 15,
};

extern cpu_t cpu;

void cpu_reset(void);
// Run until at least `n` instructions have executed. Returns instructions run.
uint64_t cpu_run(uint64_t n);
void cpu_set_irq(int line, bool state);
// Advance time without executing (used to skip recognised idle loops).
void cpu_skip(uint64_t n);
// Set when the CPU is spinning on an interrupt line that only changes on a
// scanline boundary; cpu_run returns early and the caller may skip ahead.
extern bool cpu_idle;
void cpu_exception(int code, uint32_t badvaddr, bool has_badvaddr);

// Debug hooks
extern bool cpu_trace;
const char *cpu_disasm(uint32_t pc, uint32_t op, char *buf);
