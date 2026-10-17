#pragma once
#include <stdint.h>
#include <string.h>

// Show a message at the bottom of the screen for two seconds.
void osd_message(const char *fmt, ...);
// Draw the current message, and `status` (e.g. "PAUSED") top right, into a 320x240 XRGB image.
void osd_draw(uint32_t *px, const char *status);
