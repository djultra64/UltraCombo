// Killer Instinct 2 physical bus.
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define RAM0_SIZE 0x80000u     // phys 0x00000000: 512 KB (holds the framebuffers)
#define RAM2_SIZE 0x800000u    // phys 0x08000000: 8 MB
#define ROM_SIZE  0x80000u     // phys 0x1fc00000: boot EPROM U98

extern uint8_t ram0[RAM0_SIZE];
extern uint8_t ram2[RAM2_SIZE];
extern uint8_t rom[ROM_SIZE];

// Returns a host pointer for plain memory, or NULL for I/O space.
static inline uint8_t *mem_ptr(uint32_t pa)
{
    if (pa < RAM0_SIZE) return &ram0[pa];
    if (pa - 0x08000000u < RAM2_SIZE) return &ram2[pa - 0x08000000u];
    if (pa - 0x1fc00000u < ROM_SIZE) return &rom[pa - 0x1fc00000u];
    return 0;
}

uint32_t io_read(uint32_t pa, int size);
void     io_write(uint32_t pa, uint32_t val, int size);

// Hardware state shared with the frontend
typedef struct {
    uint32_t vram_control, sound_reset, sound_control, sound_data, coin_control;
    uint32_t p1, p2, volume, dsw;   // active-low inputs
} hw_t;
extern hw_t hw;

const uint16_t *video_base(void);   // 320x240, 640-byte stride, BGR555
