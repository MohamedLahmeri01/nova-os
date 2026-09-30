/* NOVA OS interrupt interface (Phase 3).
 * trap_frame_t mirrors exactly what idt.S builds on the stack:
 *vec+err pushed by the stub, CPU-pushed eip/cs/eflags on top.
 * Exceptions are fatal by design (trap_handler panics); IRQs return.
 */
#ifndef NOVA_IRQ_H
#define NOVA_IRQ_H

#include <stdint.h>

typedef struct {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp_ignored, ebx, edx, ecx, eax;
    uint32_t vec, err;
    uint32_t eip, cs, eflags;
} trap_frame_t;

void irq_init(void);
void trap_handler(trap_frame_t *f);
void irq_handler(trap_frame_t *f);
void lapic_probe(void);

/* Delivery diagnostics (read by the timer wait). */
extern uint32_t g_irq_count;
extern uint32_t g_last_vec;

#endif /* NOVA_IRQ_H */
