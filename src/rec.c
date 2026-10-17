// Recompiled code dispatch: maps entry addresses to generated functions and
// checks that the game's code in RAM still matches the recompiled image.
#include "rec.h"
#include <stdio.h>
#include <stdlib.h>

uint32_t rec_code_lo, rec_code_size, rec_code_gen = 1;
bool     rec_enabled;
rec_fn_t *rec_map;
uint32_t rec_map_gen;

static uint32_t *func_ok_gen;   // rec_code_gen at which each function last matched RAM
static uint32_t *func_bad_gen;  // ... or last failed to match

void rec_init(void)
{
    if (!rec_nfuncs || getenv("KI2_NOREC") || getenv("KI2_COVER") || cpu_trace) return;
    rec_code_lo = rec_image_base & 0x1fffffffu;
    rec_code_size = rec_image_words * 4;
    rec_map = calloc(rec_image_words, sizeof *rec_map);
    func_ok_gen = calloc(rec_nfuncs, sizeof *func_ok_gen);
    func_bad_gen = calloc(rec_nfuncs, sizeof *func_bad_gen);
    rec_map_gen = 0;
    rec_enabled = true;
    fprintf(stderr, "rec: %u functions, %u entry points, %u KB of code\n",
            rec_nfuncs, rec_nlabels, rec_image_words * 4 / 1024);
}

static bool func_matches(uint32_t f)
{
    if (func_ok_gen[f] == rec_code_gen) return true;
    if (func_bad_gen[f] == rec_code_gen) return false;
    const rec_func_t *fn = &rec_funcs[f];
    uint32_t off = fn->lo - rec_image_base, len = fn->hi - fn->lo;
    bool ok = memcmp(&ram2[rec_code_lo - 0x08000000u + off], (const uint8_t *)rec_image + off, len) == 0;
    (ok ? func_ok_gen : func_bad_gen)[f] = rec_code_gen;
    return ok;
}

// Code was written since the map was built: rebuild it lazily, entry by entry.
// While code is being loaded (stores every few instructions) this only runs
// every 10 ms of game time; the interpreter covers the gap.
rec_fn_t rec_lookup_slow(uint32_t pc)
{
    static uint64_t last;
    if (cpu.cycles - last < 1000000 && last) return 0;
    last = cpu.cycles;
    memset(rec_map, 0, rec_image_words * sizeof *rec_map);
    for (uint32_t i = 0; i < rec_nlabels; i++) {
        const rec_label_t *l = &rec_labels[i];
        if (func_matches(l->fn)) rec_map[(l->pc - rec_image_base) >> 2] = rec_funcs[l->fn].fn;
    }
    rec_map_gen = rec_code_gen;
    return rec_map[(pc - rec_image_base) >> 2];
}
