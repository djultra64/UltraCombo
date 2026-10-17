#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

bool ide_open(const char *path, uint32_t cyls, uint32_t heads, uint32_t secs, int serial_offset);
uint32_t ide_read(int reg);
void ide_write(int reg, uint32_t v);
uint32_t ide_alt_status(void);
void ide_devctl(uint32_t v);
void ide_tick(int cycles);

// The game keeps its settings and audits in the first sectors of the disk.
// Those are tracked like RAM pages, so save states and rewind include them.
#define IDE_TRACK_BYTES (64 * 512)
#define IDE_TRACK_PAGES (IDE_TRACK_BYTES >> 12)
extern uint8_t ide_dirty[IDE_TRACK_PAGES];
uint8_t *ide_tracked(void);              // in-memory copy of the first IDE_TRACK_BYTES
void ide_tracked_changed(void);          // the copy was modified (state load/rewind)
void ide_flush(void);                    // write the copy back to the image file
void ide_power_on(void);                 // controller back to its state after ide_open
uint64_t ide_image_id(void);             // identity of the image (for netplay)

// Save state: copy the controller state to/from buf (NULL: just return the size).
size_t ide_state(uint8_t *buf, bool load);
