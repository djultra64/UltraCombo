#pragma once
#include <stdbool.h>
#include <stdint.h>

// Write a w x h image of 0x00RRGGBB pixels (up to 640x480).
bool png_write(const char *path, const uint32_t *xrgb, int w, int h);
