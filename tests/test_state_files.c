/* File admission tests use synthetic memory and fixed-size device serializers.
 * No ROM, disk image or translated game code is needed. */
#include "state.h"
#include "mem.h"
#include "ide.h"
#include "rec.h"
#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8_t ram[RAM_SIZE], rom[ROM_SIZE], ram_dirty[RAM_PAGES];
uint8_t ide_dirty[IDE_TRACK_PAGES];
bool rec_enabled;
const rec_module_t *rec_mod;
game_t game;
static uint8_t sectors[IDE_TRACK_BYTES];
static uint64_t devices[5] = {11, 22, 33, 44, 55};
static const char *path;
static int checks;

static void check(bool ok, const char *name)
{
    if (!ok) { fprintf(stderr, "FAIL: %s\n", name); exit(1); }
    checks++;
}

uint8_t *ide_tracked(void) { return sectors; }
void ide_tracked_changed(void) {}
void rec_forget(void) {}
uint64_t machine_frame(void) { return 0; }
static size_t device_state(uint8_t *p, bool load, int i)
{
    if (p) {
        if (load) memcpy(&devices[i], p, 8);
        else memcpy(p, &devices[i], 8);
    }
    return 8;
}
size_t cpu_state(uint8_t *p, bool load) { return device_state(p, load, 0); }
size_t rec_state(uint8_t *p, bool load) { return device_state(p, load, 1); }
size_t ide_state(uint8_t *p, bool load) { return device_state(p, load, 2); }
size_t sound_state(uint8_t *p, bool load) { return device_state(p, load, 3); }
size_t machine_state(uint8_t *p, bool load) { return device_state(p, load, 4); }

static void write_bytes(const void *data, size_t n)
{
    FILE *f = fopen(path, "wb");
    check(f != NULL, "open fixture");
    check(fwrite(data, 1, n, f) == n, "write fixture");
    check(fclose(f) == 0, "close fixture");
}

static void reject(const uint8_t *data, size_t n, const char *name)
{
    write_bytes(data, n);
    uint64_t before = state_hash();
    size_t history = state_history_bytes();
    check(!state_load_file(path), name);
    check(state_hash() == before, "rejection preserves all memory and device state");
    check(state_history_bytes() == history, "rejection preserves rewind history");
}

/* Version 1 header, followed by five synthetic 8-byte device states. */
typedef struct {
    char magic[8];
    uint32_t version, core_len, npages, reserved;
    uint64_t rom_id, frame;
} header_t;

static void token(uint8_t *p, uint16_t zeros, uint16_t literals)
{
    memcpy(p, &zeros, 2);
    memcpy(p + 2, &literals, 2);
}

static bool all_zero(const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++) if (p[i]) return false;
    return true;
}

int main(int argc, char **argv)
{
    check(argc == 2, "usage: test_state_files SCRATCH_FILE");
    path = argv[1];
    /* Distinct RAM, settings and devices exercise actual restoration. */
    for (size_t i = 0; i < sizeof ram; i++) ram[i] = (uint8_t)(i * 17 + i / 4096);
    memset(sectors, 0x6b, sizeof sectors);
    state_init();
    uint64_t original = state_hash();
    check(state_save_file(path), "save valid state");
    memset(ram, 0xa5, sizeof ram);
    memset(sectors, 0xc3, sizeof sectors);
    devices[0] = 99;
    check(state_load_file(path), "load valid state");
    check(state_hash() == original, "valid state restores RAM, disk settings and devices");

    /* Make a small, independently encoded all-zero page fixture. */
    FILE *f = fopen(path, "rb");
    check(f != NULL, "read header");
    header_t h;
    check(fread(&h, sizeof h, 1, f) == 1, "read saved header");
    check(h.core_len == sizeof devices, "synthetic core size");
    const size_t prefix = sizeof h + h.core_len;
    const size_t length = prefix + (size_t)h.npages * 4;
    uint8_t *data = calloc(1, length + 8);
    check(data != NULL, "allocate fixture");
    memcpy(data, &h, sizeof h);
    check(fread(data + sizeof h, 1, h.core_len, f) == h.core_len, "read saved core");
    check(fclose(f) == 0, "close saved state");
    for (uint32_t i = 0; i < h.npages; i++) token(data + prefix + (size_t)i * 4, 1024, 0);

    state_set_history(1024 * 1024);
    ram[0] ^= 1; ram_dirty[0] = 1;
    state_checkpoint();
    reject(data, prefix, "reject missing every declared page");
    reject(data, sizeof h - 1, "reject short header");
    reject(data, prefix - 1, "reject short core");
    reject(data, length - 4, "reject missing final page");
    reject(data, length - 1, "reject truncated final token");
    reject(data, length + 1, "reject trailing bytes");
    token(data + prefix, 0, 0);
    reject(data, length, "reject zero-progress token");
    token(data + prefix, 1025, 0);
    reject(data, length, "reject zero run beyond page");
    token(data + prefix, 1024, 1);
    reject(data, length, "reject literal run beyond page");
    token(data + prefix, 0, 1025);
    reject(data, length, "reject oversized literal run");
    token(data + prefix, 0, 1);
    reject(data, prefix + 4, "reject missing literal payload");
    reject(data, prefix + 7, "reject partial literal word");
    token(data + prefix, 1024, 0);
    token(data + length - 4, 0, 0);
    reject(data, length, "late failure cannot partially load earlier pages");
    token(data + length - 4, 1024, 0);
    data[0] ^= 1;
    reject(data, length, "reject wrong magic");
    data[0] ^= 1;
    data[8] ^= 1;
    reject(data, length, "reject wrong version");
    data[8] ^= 1;

    header_t *changed = (header_t *)data;
    changed->core_len++;
    reject(data, length, "reject incompatible core size");
    changed->core_len--;
    changed->npages--;
    reject(data, length, "reject incompatible page count");
    changed->npages++;
    changed->rom_id ^= 1;
    reject(data, length, "reject incompatible ROM identity");
    changed->rom_id ^= 1;
    write_bytes(data, length);
    check(state_load_file(path), "accept independently encoded zero pages");
    check(all_zero(ram, sizeof ram), "zero fixture restores every RAM byte");
    check(all_zero(sectors, sizeof sectors), "zero fixture restores every settings byte");
    check(state_history_bytes() == 0, "valid load clears old rewind history");

    memmove(data + prefix + 12, data + prefix + 4, length - prefix - 4);
    token(data + prefix, 0, 1);
    uint32_t literal = 0x12345678;
    memcpy(data + prefix + 4, &literal, 4);
    token(data + prefix + 8, 1023, 0);
    write_bytes(data, length + 8);
    check(state_load_file(path), "accept literal followed by a terminal zero run");
    check(!memcmp(ram, &literal, 4) && all_zero(ram + 4, sizeof ram - 4),
          "mixed tokens restore the exact literal and zero runs");
    free(data);
    check(remove(path) == 0, "remove fixture");
    printf("state file tests: PASS (%d checks)\n", checks);
    return 0;
}
