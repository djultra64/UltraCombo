#pragma once
#include <stdint.h>
#include <stdbool.h>

bool ide_open(const char *path, uint32_t cyls, uint32_t heads, uint32_t secs, int serial_offset);
uint32_t ide_read(int reg);
void ide_write(int reg, uint32_t v);
uint32_t ide_alt_status(void);
void ide_devctl(uint32_t v);
void ide_tick(int cycles);
