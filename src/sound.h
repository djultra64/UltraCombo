// DCS sound board interface.
#pragma once
#include <stdint.h>
#include <stdbool.h>

bool sound_init(const char *rom_dir);
void sound_reset_w(bool state);      // bit 0 of the sound reset register
void sound_data_w(uint16_t data);    // latched on rising edge of sound control bit 1
bool sound_input_empty(void);        // DCS has consumed the last command word
void sound_run(int samples);         // advance sound CPU, producing samples
int  sound_fetch(int16_t *out, int max);
