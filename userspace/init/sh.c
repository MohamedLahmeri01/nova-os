/* NOVA OS shell (Phase 6).
 * Line input with backspace over getchar; builtins: help, echo, mem,
 * ticks, clear, exit. mem/ticks read kernel state via syscalls.
 * Single binary with init (no exec yet; exec arrives with the FS).
 */
#include <stdint.h>

#include "nova.h"
#include "stdio.h"
#include "string.h"
#include "sh.h"

#define SH_LINE_MAX 128u

static void sh_putdec(uint32_t v) {
    char buf[10];
    int n = 0;
    if (v == 0) {
        nova_putchar('0');
        return;
    }
    while (v > 0 && n < 10) {
        buf[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (n > 0) {
        n--;
        nova_putchar(buf[n]);
    }
}

static void sh_help(void) {
    nova_puts("commands: help echo mem ticks clear exit");
}

static void sh_echo(const char *line) {
    const char *p = line + 4;
    while (*p == ' ') {
        p++;
    }
    nova_puts(p);
}

static void sh_mem(void) {
    uint32_t total = 0, freep = 0;
    if (nova_meminfo(&total, &freep) != 0) {
        nova_puts("meminfo failed");
        return;
    }
    nova_print("frames total=", 13);
    sh_putdec(total);
    nova_print(" free=", 6);
    sh_putdec(freep);
    nova_putchar('\n');
}

static void sh_ticks(void) {
    int32_t t = nova_ticks();
    nova_print("ticks=", 6);
    sh_putdec((uint32_t)t);
    nova_putchar('\n');
}

static void sh_clear(void) {
    for (int i = 0; i < 24; i++) {
        nova_putchar('\n');
    }
}

static void sh_run(const char *line) {
    if (line[0] == 0) {
        return;
    } else if (nova_strcmp(line, "help") == 0) {
        sh_help();
    } else if (nova_strncmp(line, "echo", 4) == 0) {
        sh_echo(line);
    } else if (nova_strcmp(line, "mem") == 0) {
        sh_mem();
    } else if (nova_strcmp(line, "ticks") == 0) {
        sh_ticks();
    } else if (nova_strcmp(line, "clear") == 0) {
        sh_clear();
    } else if (nova_strcmp(line, "exit") == 0) {
        nova_puts("bye");
        nova_exit(0);
    } else {
        nova_puts("unknown: try help");
    }
}

void sh_main(void) {
    char line[SH_LINE_MAX];
    nova_puts("nova sh (Phase 6) - type help");
    for (;;) {
        uint32_t n = 0;
        nova_print("> ", 2);
        for (;;) {
            int c = nova_getchar();
            if (c == '\n' || c == '\r') {
                nova_putchar('\n');
                break;
            } else if (c == '\b' || c == 127) {
                if (n > 0) {
                    n--;
                    nova_print("\b \b", 3);
                }
            } else if (c >= 32 && c < 127 && n + 1 < SH_LINE_MAX) {
                line[n++] = (char)c;
                nova_putchar((char)c);
            }
        }
        line[n] = 0;
        sh_run(line);
    }
}
