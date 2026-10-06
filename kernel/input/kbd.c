/* NOVA OS PS/2 keyboard driver (Phase 6, blocking getkey 16-i).
 * IRQ1 pushes Set-1 scancodes through a US-layout translator (shift
 * states, backspace/enter; extended 0xE0 sequences dropped for now)
 * into a 256-byte ring. kbd_getkey blocks when scheduled (the IRQ
 * push wakes the single waiter); -EAGAIN only pre-scheduler, and
 * -EBUSY when another thread already parks here. cli/sti guards
 * the head/tail against IRQ races (short, bounded).
 */
#include <stdint.h>

#include "kbd.h"
#include "sched/sched.h"
#include "thread/thread.h"
#include "process/process.h"
#include "syscall/syscall.h"
#include "serial.h"

#if defined(__i386__)
#include "../arch/x86/io.h"
#include "../arch/x86/cpu.h"
#elif defined(__x86_64__)
#include "../arch/x86_64/io.h"
#include "../arch/x86_64/cpu.h"
#else
#error "kbd: no arch backend for this architecture"
#endif

#define KBD_DATA 0x60u
#define KBD_BUF 256u
#define KBD_EAGAIN (-11)

static volatile uint8_t g_buf[KBD_BUF];
static volatile uint32_t g_head;
static volatile uint32_t g_tail;
static int g_shift;
static int g_escaped;
/* Parked reader (one; woken on push). */
static struct thread *g_waiter;

static char scancode_to_ascii(uint8_t sc, int shift) {
    switch (sc) {
    case 0x02:
        return shift ? '!' : '1';
    case 0x03:
        return shift ? '@' : '2';
    case 0x04:
        return shift ? '#' : '3';
    case 0x05:
        return shift ? '$' : '4';
    case 0x06:
        return shift ? '%' : '5';
    case 0x07:
        return shift ? '^' : '6';
    case 0x08:
        return shift ? '&' : '7';
    case 0x09:
        return shift ? '*' : '8';
    case 0x0A:
        return shift ? '(' : '9';
    case 0x0B:
        return shift ? ')' : '0';
    case 0x0C:
        return shift ? '_' : '-';
    case 0x0D:
        return shift ? '+' : '=';
    case 0x0E:
        return '\b';
    case 0x0F:
        return '\t';
    case 0x1C:
        return '\n';
    case 0x39:
        return ' ';
    default:
        break;
    }
    if (sc >= 0x10 && sc <= 0x19) {
        static const char *row = "qwertyuiop";
        char c = row[sc - 0x10];
        return shift ? (char)(c - 32) : c;
    }
    if (sc >= 0x1E && sc <= 0x26) {
        static const char *row = "asdfghjkl";
        char c = row[sc - 0x1E];
        return shift ? (char)(c - 32) : c;
    }
    if (sc >= 0x2C && sc <= 0x32) {
        static const char *row = "zxcvbnm";
        char c = row[sc - 0x2C];
        return shift ? (char)(c - 32) : c;
    }
    switch (sc) {
    case 0x1A:
        return shift ? '{' : '[';
    case 0x1B:
        return shift ? '}' : ']';
    case 0x27:
        return shift ? ':' : ';';
    case 0x28:
        return shift ? '"' : '\'';
    case 0x29:
        return shift ? '~' : '`';
    case 0x2B:
        return shift ? '|' : '\\';
    case 0x33:
        return shift ? '<' : ',';
    case 0x34:
        return shift ? '>' : '.';
    case 0x35:
        return shift ? '?' : '/';
    default:
        return 0;
    }
}

void kbd_init(void) {
    g_head = 0;
    g_tail = 0;
    g_shift = 0;
    g_escaped = 0;
    g_waiter = 0;
}

static void buf_push(char c) {
    uint32_t next = (g_tail + 1u) % KBD_BUF;
    if (next != g_head) {
        g_buf[g_tail] = (uint8_t)c;
        g_tail = next;
    }
    /* A parked reader wakes with the data. Wake-then-clear: a
     * newcomer between the two sees a waiter (EBUSY) instead of
     * stealing the wake. IRQ-safe (stores + wakeup only). */
    if (g_waiter != 0) {
        struct thread *w = g_waiter;
        sched_wakeup(w);
        g_waiter = 0;
    }
}

/* One scancode through the translator (the IRQ path and the
 * selftest share it; the IRQ half is just the port read). */
void kbd_feed(uint8_t sc) {
    if (sc == 0xE0) {
        g_escaped = 1;
        return;
    }
    if (sc & 0x80u) {
        uint8_t code = sc & 0x7Fu;
        if (!g_escaped && (code == 0x2A || code == 0x36)) {
            g_shift = 0;
        }
        g_escaped = 0;
        return;
    }
    if (g_escaped) {
        g_escaped = 0;
        return;
    }
    if (sc == 0x2A || sc == 0x36) {
        g_shift = 1;
        return;
    }
    {
        char c = scancode_to_ascii(sc, g_shift);
        if (c != 0) {
            buf_push(c);
        }
    }
}

void kbd_irq(void) {
    kbd_feed(arch_inb(KBD_DATA));
}

int32_t kbd_getkey(void) {
    int32_t r;
    struct thread *me;
    /* Fast path: key waiting (no sleep, any context). */
    arch_cli();
    if (g_head != g_tail) {
        r = g_buf[g_head];
        g_head = (g_head + 1u) % KBD_BUF;
        arch_sti();
        return r;
    }
    arch_sti();
    /* Pre-scheduler: legacy poll (selftests at boot). */
    if (!sched_can_sleep()) {
        return KBD_EAGAIN;
    }
    /* Blocking path: park till the IRQ push wakes us. Check +
     * park hold IF=0 throughout (a push between them would
     * wake nobody and strand us: the lost-wakeup). One waiter:
     * another parker is EBUSY (never overwrite: that strands
     * them). The push is the only waker, so anything woken
     * finds data; the loop is defensive. */
    me = sched_current_thread();
    for (;;) {
        arch_cli();
        if (g_head != g_tail) {
            r = g_buf[g_head];
            g_head = (g_head + 1u) % KBD_BUF;
            arch_sti();
            return r;
        }
        if (g_waiter != 0 && g_waiter != me) {
            arch_sti();
            return -NOVA_EBUSY;
        }
        g_waiter = me;
        sched_sleep(); /* IF=0 in, IF=1 out; recheck under cli */
    }
}

/* --- blocking proof (scheduler live): park, wake, EBUSY ---
 * Worker parks in getkey (empty ring) while main watches
 * BLOCKED; fed scancodes wake it with exact chars in order.
 * A second parker then proves EBUSY (main refused, worker
 * released by a spare key). Prints KBDBLK-OK. */

static volatile int kb_done;
static volatile int32_t kb_c0;
static volatile int32_t kb_c1;
static volatile int32_t kb_c2;
static struct thread *kb_worker;

static void kbd_reader(void *arg) {
    (void)arg;
    kb_c0 = kbd_getkey();
    kb_c1 = kbd_getkey();
    kb_done = 1;
    thread_exit(0);
}

static void kbd_reader_one(void *arg) {
    (void)arg;
    kb_c2 = kbd_getkey();
    kb_done = 1;
    thread_exit(0);
}

static int kbd_wait_blocked(uint32_t t0) {
    extern volatile uint32_t g_ticks;
    while (kb_worker->state != THREAD_BLOCKED) {
        if (g_ticks - t0 > 2000u) {
            return -1;
        }
        __asm__ volatile("hlt");
    }
    return 0;
}

int kbd_block_selftest(void) {
    extern volatile uint32_t g_ticks;
    struct thread *w;
    uint32_t t0;
    int32_t r;
    kb_done = 0;
    kb_c0 = -1;
    kb_c1 = -1;
    kb_c2 = -1;
    /* Ring is empty (no keys pressed during boot); scheduler +
     * timer live (runs after the sleep selftest). */
    w = thread_create(g_init_process, kbd_reader, 0, 16u);
    if (w == 0) {
        serial_puts("KBDBLK-FAIL spawn\n");
        return -1;
    }
    kb_worker = w;
    sched_yield(); /* worker runs to its parked getkey */
    /* Parked with nothing queued; fed 'h','i' arrive in order
     * (Set-1: 0x23, 0x17; ring buffers both before reading). */
    t0 = g_ticks;
    if (kbd_wait_blocked(t0) != 0) {
        serial_puts("KBDBLK-FAIL park\n");
        return -1;
    }
    kbd_feed(0x23u);
    kbd_feed(0x17u);
    t0 = g_ticks;
    while (!kb_done) {
        if (g_ticks - t0 > 2000u) {
            serial_puts("KBDBLK-FAIL wake\n");
            return -1;
        }
        __asm__ volatile("hlt");
    }
    if (kb_c0 != 'h' || kb_c1 != 'i') {
        serial_puts("KBDBLK-FAIL chars\n");
        return -1;
    }
    /* Second parker: main is refused (EBUSY, never parks), the
     * worker is released by a spare Enter. */
    kb_done = 0;
    w = thread_create(g_init_process, kbd_reader_one, 0, 16u);
    if (w == 0) {
        serial_puts("KBDBLK-FAIL spawn2\n");
        return -1;
    }
    kb_worker = w;
    sched_yield();
    t0 = g_ticks;
    if (kbd_wait_blocked(t0) != 0) {
        serial_puts("KBDBLK-FAIL park2\n");
        return -1;
    }
    r = kbd_getkey();
    if (r != -NOVA_EBUSY) {
        serial_puts("KBDBLK-FAIL ebusy\n");
        return -1;
    }
    kbd_feed(0x1Cu); /* Enter */
    t0 = g_ticks;
    while (!kb_done) {
        if (g_ticks - t0 > 2000u) {
            serial_puts("KBDBLK-FAIL wake2\n");
            return -1;
        }
        __asm__ volatile("hlt");
    }
    if (kb_c2 != '\n') {
        serial_puts("KBDBLK-FAIL spare\n");
        return -1;
    }
    serial_puts("KBDBLK-OK\n");
    return NOVA_ESUCCESS;
}
