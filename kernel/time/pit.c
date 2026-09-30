/* NOVA OS timer: PIT channel 0 at 100Hz (Phase 3).
 * IRQ0 bumps g_ticks; timer_init enables delivery (sti) and proves
 * liveness by observing ticks via hlt-wait (panics on timer-dead).
 * Note: a fully dead PIT would sleep in hlt forever (no watchdog yet);
 * the bounded loop catches slow/missing IRQs short of that.
 */
#include <stdint.h>

#include "serial.h"
#include "panic.h"
#include "time.h"

#if defined(__i386__)
#include "../arch/x86/io.h"
#include "../arch/x86/cpu.h"
#elif defined(__x86_64__)
#include "../arch/x86_64/io.h"
#include "../arch/x86_64/cpu.h"
#else
#error "pit: no arch backend for this architecture"
#endif

#define PIT_CH0 0x40u
#define PIT_CMD 0x43u
#define PIT_HZ 1193182u
#define PIT_WANT_HZ 100u
#define PIT_WANT_TICKS 20u

volatile uint32_t g_ticks;

void timer_init(void) {
    uint32_t div = PIT_HZ / PIT_WANT_HZ;
    arch_outb(PIT_CMD, 0x36u);
    arch_outb(PIT_CH0, (uint8_t)(div & 0xFFu));
    arch_outb(PIT_CH0, (uint8_t)((div >> 8) & 0xFFu));
    arch_sti();
    {
        uint32_t t0 = g_ticks;
        for (uint32_t i = 0; i < 10000u && g_ticks - t0 < PIT_WANT_TICKS;
             i++) {
            arch_hlt();
        }
        if (g_ticks - t0 < PIT_WANT_TICKS) {
            nova_panic("timer-dead");
        }
    }
    serial_puts("TIMER-OK ticks=");
    serial_putdec32(g_ticks);
    serial_putc('\n');
}
