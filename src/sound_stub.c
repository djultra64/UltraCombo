// Placeholder DCS: accepts every command immediately and produces silence.
#include "sound.h"
#include <stdio.h>
#include <stdlib.h>

bool sound_init(const char *rom_dir) { (void)rom_dir; return true; }
void sound_reset_w(bool state) { (void)state; }
void sound_data_w(uint16_t data) { if (getenv("KI2_LOG_SOUND")) fprintf(stderr, "dcs: cmd %04x\n", data); }
bool sound_input_empty(void) { return true; }
void sound_run(int samples) { (void)samples; }
int  sound_fetch(int16_t *out, int max) { (void)out; (void)max; return 0; }
