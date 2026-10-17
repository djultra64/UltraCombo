// Analog Devices ADSP-2105 DSP core (as used on the Midway DCS sound board).
// Behaviour cross-checked against MAME's ADSP-21xx core (BSD-3-Clause, Aaron Giles).
#pragma once
#include <stdint.h>
#include <stdbool.h>

enum { ADSP_IRQ0 = 0, ADSP_IRQ1 = 1, ADSP_IRQ2 = 2, ADSP_TIMER = 5 };

typedef struct {
    // board hooks for external memory
    uint32_t (*pm_read)(uint16_t addr);            // 24-bit program word
    void     (*pm_write)(uint16_t addr, uint32_t v);
    uint16_t (*dm_read)(uint16_t addr);
    void     (*dm_write)(uint16_t addr, uint16_t v);
    void     (*sample_out)(int16_t s);             // one SPORT1 sample
    void     (*sample_rate)(double hz);            // SPORT1 rate changed
    void     (*sysctl_reboot)(void);               // SYSCONTROL bit 9 soft reboot
} adsp_hooks_t;

void adsp_init(const adsp_hooks_t *hooks);
void adsp_reset(void);
void adsp_run(int cycles);
void adsp_set_irq(int line, bool state);
void adsp_pulse_irq(int line);
uint32_t *adsp_internal_pm(void);   // 1K x 24-bit, filled by the boot loader
uint16_t adsp_pc(void);
