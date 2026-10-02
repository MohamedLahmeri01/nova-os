/* NOVA OS PS/2 keyboard driver (Phase 6).
 * IRQ1 pushes Set-1 scancodes through a US-layout translator (shift
 * states, backspace/enter; extended 0xE0 sequences dropped for now)
 * into a 256-byte ring. kbd_getkey returns -EAGAIN when empty;
 * cli/sti guards the head/tail against IRQ races (short, bounded).
 */
#include <stdint.h>

#include "kbd.h"

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
}

static void buf_push(char c) {
    uint32_t next = (g_tail + 1u) % KBD_BUF;
    if (next != g_head) {
        g_buf[g_tail] = (uint8_t)c;
        g_tail = next;
    }
}

void kbd_irq(void) {
    uint8_t sc = arch_inb(KBD_DATA);
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

int32_t kbd_getkey(void) {
    int32_t r;
    arch_cli();
    if (g_head == g_tail) {
        r = KBD_EAGAIN;
    } else {
        r = g_buf[g_head];
        g_head = (g_head + 1u) % KBD_BUF;
    }
    arch_sti();
    return r;
}
