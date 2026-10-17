// The emulated machine as the frontends see it.
#pragma once
#include <stddef.h>
#include <stdint.h>

#define FRAME_HZ 58.9634              // KI2 video refresh

void run_frame(void);                 // one video frame of CPU, disk and sound
void to_rgb(uint32_t *dst);           // current 320x240 frame as 0x00RRGGBB
const char *base_dir(void);           // directory of the executable, with trailing separator
const char *find_data(const char *name, char *buf, size_t n);
int  frontend_main(void);             // windowed mode (frontend.c)
