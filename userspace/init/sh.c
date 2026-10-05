/* NOVA OS shell (Phase 6 init, Phase 7 files).
 * Line input with backspace over getchar; builtins: help, echo, mem,
 * ticks, ls, cat, mkdir, clear, exit. mem/ticks read kernel state via
 * syscalls; ls/cat/mkdir exercise the VFS (open/read/readdir/mkdir).
 * Single binary with init (no exec yet; exec arrives with the FS).
 */
#include <stdint.h>

#include "nova/nova.h"
#include "nova/stdio.h"
#include "nova/string.h"
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

static const char *sh_arg(const char *line);
static void sh_denied(const char *what);

static void sh_help(void) {
    nova_puts("commands: help echo mem ticks ls cat mkdir run put fsck ping ip dhcp caps dropcap rlimit id setuid chmod chroot clear exit");
}

static void sh_putip(uint32_t ip) {
    for (int q = 3; q >= 0; q--) {
        sh_putdec((ip >> (uint32_t)(q * 8)) & 0xFFu);
        if (q != 0) {
            nova_putchar('.');
        }
    }
}

static void sh_ip(void) {
    uint32_t self = 0, gw = 0, mask = 0;
    if (nova_netinfo(&self, &gw, &mask) != 0) {
        nova_puts("netinfo failed");
        return;
    }
    nova_print("ip=", 3);
    sh_putip(self);
    nova_print(" mask=", 6);
    sh_putip(mask);
    nova_print(" gw=", 4);
    sh_putip(gw);
    nova_putchar('\n');
}

static void sh_dhcp(void) {
    int32_t r = nova_dhcp();
    if (r != 0) {
        nova_puts("dhcp failed");
        return;
    }
    sh_ip();
}

static void sh_caps(void) {
    int32_t r = nova_caps(CAP_OP_GET, 0);
    if (r < 0) {
        nova_puts("caps failed");
        return;
    }
    nova_print("caps=", 5);
    if ((r & CAP_NET) != 0) {
        nova_print("net ", 4);
    }
    if ((r & CAP_SPAWN) != 0) {
        nova_print("spawn ", 6);
    }
    if (r == 0) {
        nova_print("none", 4);
    }
    nova_putchar('\n');
}

static void sh_dropcap(const char *line) {
    const char *p = sh_arg(line);
    uint32_t mask = 0;
    int32_t r;
    if (nova_strcmp(p, "net") == 0) {
        mask = CAP_NET;
    } else if (nova_strcmp(p, "spawn") == 0) {
        mask = CAP_SPAWN;
    } else if (nova_strcmp(p, "all") == 0) {
        mask = CAP_NET | CAP_SPAWN;
    } else {
        nova_puts("usage: dropcap net|spawn|all");
        return;
    }
    r = nova_caps(CAP_OP_DROP, mask);
    if (r < 0) {
        nova_puts("dropcap failed");
        return;
    }
    sh_caps();
}

static void sh_rlshow(void) {
    int32_t v;
    v = nova_rlimit(RLIM_OP_GET, RLIM_NOFD, 0);
    nova_print("nofd=", 5);
    sh_putdec((uint32_t)v);
    v = nova_rlimit(RLIM_OP_GET, RLIM_FSIZE, 0);
    nova_print(" fsize=", 7);
    sh_putdec((uint32_t)v);
    v = nova_rlimit(RLIM_OP_GET, RLIM_FRAMES, 0);
    nova_print(" frames=", 8);
    sh_putdec((uint32_t)v);
    nova_putchar('\n');
}

static void sh_rlimit(const char *line) {
    /* rlimit | rlimit <nofd|fsize|frames> <value>: observe or
     * lower (raising refuses: limits only tighten). */
    const char *p = sh_arg(line);
    char name[8];
    uint32_t i = 0;
    uint32_t which = 0;
    int ok = 0;
    int32_t r;
    int val;
    if (*p == 0) {
        sh_rlshow();
        return;
    }
    while (*p != 0 && *p != ' ' && i + 1u < sizeof(name)) {
        name[i++] = *p++;
    }
    name[i] = 0;
    while (*p == ' ') {
        p++;
    }
    if (nova_strcmp(name, "nofd") == 0) {
        which = RLIM_NOFD;
    } else if (nova_strcmp(name, "fsize") == 0) {
        which = RLIM_FSIZE;
    } else if (nova_strcmp(name, "frames") == 0) {
        which = RLIM_FRAMES;
    } else {
        nova_puts("usage: rlimit [nofd|fsize|frames value]");
        return;
    }
    val = nova_strtoi(p, &ok);
    if (!ok || val < 0) {
        nova_puts("usage: rlimit [nofd|fsize|frames value]");
        return;
    }
    r = nova_rlimit(RLIM_OP_SET, which, (uint32_t)val);
    if (r < 0) {
        nova_puts("rlimit refused");
        return;
    }
    sh_rlshow();
}

static void sh_id(void) {
    int32_t u = nova_cred(CRED_OP_GETUID, 0);
    int32_t g = nova_cred(CRED_OP_GETGID, 0);
    if (u < 0 || g < 0) {
        nova_puts("id failed");
        return;
    }
    nova_print("uid=", 4);
    sh_putdec((uint32_t)u);
    nova_print(" gid=", 5);
    sh_putdec((uint32_t)g);
    nova_putchar('\n');
}

static void sh_setuid(const char *line) {
    const char *p = sh_arg(line);
    int ok = 0;
    int val;
    int32_t r;
    if (*p == 0) {
        nova_puts("usage: setuid <uid>");
        return;
    }
    val = nova_strtoi(p, &ok);
    if (!ok || val < 0) {
        nova_puts("usage: setuid <uid>");
        return;
    }
    r = nova_cred(CRED_OP_SETUID, (uint32_t)val);
    if (r < 0) {
        nova_puts("setuid refused");
        return;
    }
    sh_id();
}

static void sh_chmod(const char *line) {
    /* chmod <mode> <path>: mode is three decimal digits read as
     * octal (600 = owner-rw). Only ramfs modes change (grafts are
     * EROFS); only the owner or root may change them. */
    const char *p = sh_arg(line);
    char digits[4];
    uint32_t i = 0;
    uint32_t mode;
    const char *path;
    int32_t r;
    while (*p != 0 && *p != ' ' && i < 3u) {
        if (*p < '0' || *p > '7') {
            nova_puts("usage: chmod <mode> <path>");
            return;
        }
        digits[i++] = *p++;
    }
    if (i != 3u || (*p != 0 && *p != ' ')) {
        nova_puts("usage: chmod <mode> <path>");
        return;
    }
    digits[3] = 0;
    mode = (uint32_t)(digits[0] - '0') * 64u +
           (uint32_t)(digits[1] - '0') * 8u +
           (uint32_t)(digits[2] - '0');
    while (*p == ' ') {
        p++;
    }
    path = p;
    if (*path == 0) {
        nova_puts("usage: chmod <mode> <path>");
        return;
    }
    r = nova_chmod(path, mode);
    if (r == -NOVA_EACCES) {
        sh_denied("chmod");
        return;
    }
    if (r != 0) {
        nova_puts("chmod failed");
        return;
    }
    nova_puts("chmod ok");
}

static void sh_chroot(const char *line) {
    /* chroot <dir>: narrow this session's view inward (never
     * outward: the target resolves inside the current view). */
    const char *path = sh_arg(line);
    int32_t r;
    if (*path == 0) {
        nova_puts("usage: chroot <dir>");
        return;
    }
    r = nova_chroot(path);
    if (r != 0) {
        nova_puts("chroot failed");
        return;
    }
    nova_puts("chroot ok");
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

static const char *sh_arg(const char *line) {
    const char *p = line;
    while (*p != 0 && *p != ' ') {
        p++;
    }
    while (*p == ' ') {
        p++;
    }
    return p;
}

static void sh_denied(const char *what) {
    nova_print(what, nova_strlen(what));
    nova_puts(": denied");
}

static void sh_ls(const char *line) {
    const char *path = sh_arg(line);
    char name[32];
    uint32_t i = 0;
    if (*path == 0) {
        path = "/";
    }
    for (;;) {
        int32_t rc = nova_readdir(path, i, name);
        if (rc == -2) { /* -ENOENT: end of listing */
            break;
        }
        if (rc < 0) {
            if (rc == -NOVA_EACCES) {
                sh_denied("ls");
            } else {
                nova_puts("ls failed");
            }
            return;
        }
        nova_puts(name);
        i++;
        if (i > 64u) {
            break;
        }
    }
}

static void sh_cat(const char *line) {
    const char *path = sh_arg(line);
    char buf[256];
    int32_t fd;
    if (*path == 0) {
        nova_puts("usage: cat <path>");
        return;
    }
    fd = nova_open(path, NOVA_O_RDONLY);
    if (fd < 0) {
        if (fd == -NOVA_EACCES) {
            sh_denied("cat");
        } else {
            nova_puts("cat: no such file");
        }
        return;
    }
    for (;;) {
        int32_t n = nova_read(fd, buf, sizeof(buf));
        if (n <= 0) {
            break;
        }
        nova_print(buf, (uint32_t)n);
    }
    nova_close(fd);
}

static void sh_mkdir(const char *line) {
    const char *path = sh_arg(line);
    int32_t r;
    if (*path == 0) {
        nova_puts("usage: mkdir <path>");
        return;
    }
    r = nova_mkdir(path);
    if (r == -NOVA_EACCES) {
        sh_denied("mkdir");
    } else if (r != 0) {
        nova_puts("mkdir failed");
    }
}

static void sh_put(const char *line) {
    const char *p = sh_arg(line);
    char path[64];
    const char *text;
    uint32_t i = 0;
    int32_t fd;
    uint32_t n;
    while (*p != 0 && *p != ' ' && i + 1u < sizeof(path)) {
        path[i++] = *p++;
    }
    path[i] = 0;
    while (*p == ' ') {
        p++;
    }
    text = p;
    if (path[0] == 0 || *text == 0) {
        nova_puts("usage: put <path> <text>");
        return;
    }
    fd = nova_open(path, NOVA_O_WRONLY | NOVA_O_CREAT);
    if (fd < 0) {
        if (fd == -NOVA_EACCES) {
            sh_denied("put");
        } else {
            nova_puts("put: open failed");
        }
        return;
    }
    n = nova_strlen(text);
    if (nova_write(fd, text, n) != (int32_t)n) {
        nova_puts("put: write failed");
    }
    nova_close(fd);
}

static void sh_fsck(const char *line) {
    int32_t r;
    (void)line;
    r = nova_fsck();
    if (r == 0) {
        nova_puts("fsck ok");
    } else if (r < 0) {
        nova_puts("fsck failed");
    } else {
        nova_print("fsck problems=", 15);
        sh_putdec((uint32_t)r);
        nova_putchar('\n');
    }
}

static void sh_ping(const char *line) {
    /* ping [a.b.c.d]: dotted quad, octets 0..255, default
     * 127.0.0.1. Loopback plus on-link/gateway wire destinations;
     * off-subnet is EHOSTUNREACH ("no route to host").
     * (nova_strtoi needs all-digit strings, so octets parse here.) */
    const char *p = sh_arg(line);
    uint32_t addr = 0x7F000001u;
    uint32_t seq = 1;
    if (*p != 0) {
        addr = 0;
        for (int q = 0; q < 4; q++) {
            int val = 0;
            int n = 0;
            while (*p >= '0' && *p <= '9') {
                val = val * 10 + (*p - '0');
                n++;
                p++;
            }
            if (n == 0 || n > 3 || val > 255) {
                nova_puts("usage: ping [a.b.c.d]");
                return;
            }
            addr = (addr << 8) | (uint32_t)val;
            if (q < 3) {
                if (*p != '.') {
                    nova_puts("usage: ping [a.b.c.d]");
                    return;
                }
                p++;
            } else if (*p != 0) {
                nova_puts("usage: ping [a.b.c.d]");
                return;
            }
        }
    }
    {
        int32_t r = nova_ping(addr, seq);
        if (r == -NOVA_EHOSTUNREACH) {
            nova_puts("no route to host");
        } else if (r < 0) {
            nova_puts("ping failed");
        } else {
            nova_print("reply seq=", 10);
            sh_putdec(seq);
            nova_print(" time=", 6);
            sh_putdec((uint32_t)r);
            nova_putchar('\n');
        }
    }
}

static void sh_exec(const char *line) {
    const char *path = sh_arg(line);
    int32_t pid;
    if (*path == 0) {
        nova_puts("usage: run <path>");
        return;
    }
    pid = nova_exec(path);
    if (pid < 0) {
        nova_puts("run failed");
        return;
    }
    nova_print("child pid=", 10);
    sh_putdec((uint32_t)pid);
    nova_putchar('\n');
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
    } else if (nova_strncmp(line, "ls", 2) == 0 &&
               (line[2] == 0 || line[2] == ' ')) {
        sh_ls(line);
    } else if (nova_strncmp(line, "cat", 3) == 0 &&
               (line[3] == 0 || line[3] == ' ')) {
        sh_cat(line);
    } else if (nova_strncmp(line, "mkdir", 5) == 0 &&
               (line[5] == 0 || line[5] == ' ')) {
        sh_mkdir(line);
    } else if (nova_strcmp(line, "clear") == 0) {
        sh_clear();
    } else if (nova_strncmp(line, "run", 3) == 0 &&
               (line[3] == 0 || line[3] == ' ')) {
        sh_exec(line);
    } else if (nova_strncmp(line, "put", 3) == 0 &&
               (line[3] == 0 || line[3] == ' ')) {
        sh_put(line);
    } else if (nova_strcmp(line, "fsck") == 0) {
        sh_fsck(line);
    } else if (nova_strncmp(line, "ping", 4) == 0 &&
               (line[4] == 0 || line[4] == ' ')) {
        sh_ping(line);
    } else if (nova_strcmp(line, "ip") == 0) {
        sh_ip();
    } else if (nova_strcmp(line, "dhcp") == 0) {
        sh_dhcp();
    } else if (nova_strcmp(line, "caps") == 0) {
        sh_caps();
    } else if (nova_strncmp(line, "dropcap", 7) == 0 &&
               (line[7] == 0 || line[7] == ' ')) {
        sh_dropcap(line);
    } else if (nova_strncmp(line, "rlimit", 6) == 0 &&
               (line[6] == 0 || line[6] == ' ')) {
        sh_rlimit(line);
    } else if (nova_strcmp(line, "id") == 0) {
        sh_id();
    } else if (nova_strncmp(line, "setuid", 6) == 0 &&
               (line[6] == 0 || line[6] == ' ')) {
        sh_setuid(line);
    } else if (nova_strncmp(line, "chmod", 5) == 0 &&
               (line[5] == 0 || line[5] == ' ')) {
        sh_chmod(line);
    } else if (nova_strncmp(line, "chroot", 6) == 0 &&
               (line[6] == 0 || line[6] == ' ')) {
        sh_chroot(line);
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
