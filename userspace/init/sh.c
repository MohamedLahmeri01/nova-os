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
static void sh_err(const char *what, int32_t rc);
static const char *sh_nextnum(const char *p, int *val);
static void sh_putstrn(const char *s, uint32_t max);

#ifndef NOVA_VERSION
#define NOVA_VERSION "dev"
#endif

/* Authoritative command table (Phase N.2, Track A):
 * every dispatched name appears here exactly once, with
 * the same usage text its handler prints. The host test
 * enforces table<->dispatch consistency, so help cannot
 * advertise what dispatch cannot do. bare_usage=1 means
 * the bare name prints usage (verified per handler). */
struct sh_cmdinfo {
    const char *name;
    const char *desc;
    const char *usage;
    int bare_usage;
};

static const struct sh_cmdinfo sh_cmds[] = {
    {"help", "list commands (help <cmd> for usage)",
     "help [cmd]", 0},
    {"echo", "print arguments", "echo [text]", 0},
    {"mem", "memory frames total/free", 0, 0},
    {"ticks", "kernel tick counter", 0, 0},
    {"uptime", "ticks and nominal seconds", 0, 0},
    {"about", "build and ABI identity", 0, 0},
    {"rt", "thread runtime/state/prio",
     "usage: rt [tid]", 0},
    {"ls", "list directory", "ls [dir]", 0},
    {"cat", "print a file", "usage: cat <path>", 1},
    {"mkdir", "make a directory", "usage: mkdir <path>", 1},
    {"clear", "blank the screen", 0, 0},
    {"run", "execute a program", "usage: run <path>", 1},
    {"put", "write text to a file", "usage: put <path> <text>", 1},
    {"fsck", "check the native filesystem", 0, 0},
    {"ping", "ping (default gateway)", "usage: ping [a.b.c.d]", 0},
    {"ip", "self/gateway/netmask", 0, 0},
    {"dhcp", "run DHCP", 0, 0},
    {"caps", "capability mask", 0, 0},
    {"dropcap", "drop capabilities",
     "usage: dropcap net|spawn|all", 1},
    {"rlimit", "show or lower limits",
     "usage: rlimit [nofd|fsize|frames|ticks value]", 0},
    {"id", "uid and gid", 0, 0},
    {"setuid", "set uid", "usage: setuid <uid>", 1},
    {"chmod", "change mode (octal digits)",
     "usage: chmod <mode> <path>", 1},
    {"chroot", "narrow session view", "usage: chroot <dir>", 1},
    {"focus", "focused window id", 0, 0},
    {"wins", "list widget windows", 0, 0},
    {"winopen", "open a widget window",
     "usage: winopen x y w h color", 1},
    {"winclose", "close a widget window",
     "usage: winclose id", 1},
    {"wintitle", "retitle a widget window",
     "usage: wintitle id text", 1},
    {"click", "click at pixels (mouseless)",
     "usage: click x y", 1},
    {"rmdir", "remove an empty dir", "usage: rmdir <dir>", 1},
    {"uloop", "loopback UDP send+receive",
     "usage: uloop <port> <text>", 1},
    {"nslookup", "DNS A lookup", "usage: nslookup <name>", 1},
    {"stat", "ticks/switches/irqs/threads/frames", 0, 0},
    {"rm", "delete a file", "usage: rm <path>", 1},
    {"!!", "repeat the last command", "!!", 0},
    {"exit", "end the session (reset VM after)", 0, 0},
};

#define SH_NCMDS (sizeof(sh_cmds) / sizeof(sh_cmds[0]))

static const struct sh_cmdinfo *sh_lookup(const char *name) {
    uint32_t i;
    for (i = 0; i < SH_NCMDS; i++) {
        const char *n = sh_cmds[i].name;
        uint32_t k = 0;
        while (n[k] != 0 && n[k] == name[k]) {
            k++;
        }
        if (n[k] == 0 && (name[k] == 0 || name[k] == ' ')) {
            return &sh_cmds[i];
        }
    }
    return 0;
}

static void sh_help(const char *line) {
    const char *p = sh_arg(line);
    uint32_t i;
    if (*p == 0) {
        /* Compact name list (fits the 25-row console:
         * descriptions live under help <cmd>). */
        nova_print("commands:", 9);
        for (i = 0; i < SH_NCMDS; i++) {
            nova_putchar(' ');
            nova_print(sh_cmds[i].name,
                       nova_strlen(sh_cmds[i].name));
        }
        nova_putchar('\n');
        return;
    }
    {
        const struct sh_cmdinfo *c = sh_lookup(p);
        if (c == 0) {
            nova_puts("unknown: try help");
            return;
        }
        nova_print(c->name, nova_strlen(c->name));
        nova_print(" - ", 3);
        nova_puts(c->desc);
        if (c->usage != 0) {
            nova_puts(c->usage);
        } else {
            nova_puts("(no arguments)");
        }
    }
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
    int32_t r = nova_netinfo(&self, &gw, &mask);
    if (r != 0) {
        sh_err("netinfo", r);
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
        sh_err("dhcp", r);
        return;
    }
    sh_ip();
}

static void sh_caps(void) {
    int32_t r = nova_caps(CAP_OP_GET, 0);
    if (r < 0) {
        sh_err("caps", r);
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
        sh_err("dropcap", r);
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
    v = nova_rlimit(RLIM_OP_GET, RLIM_TICKS, 0);
    nova_print(" ticks=", 7);
    sh_putdec((uint32_t)v);
    nova_putchar('\n');
}

static void sh_rlimit(const char *line) {
    /* rlimit | rlimit <nofd|fsize|frames|ticks> <value>: observe
     * or lower (raising refuses: limits only tighten). */
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
    } else if (nova_strcmp(name, "ticks") == 0) {
        which = RLIM_TICKS;
    } else {
        nova_puts("usage: rlimit [nofd|fsize|frames|ticks value]");
        return;
    }
    val = nova_strtoi(p, &ok);
    if (!ok || val < 0) {
        nova_puts("usage: rlimit [nofd|fsize|frames|ticks value]");
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
        sh_err("id", u < 0 ? u : g);
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
        sh_err("chmod", r);
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
        sh_err("chroot", r);
        return;
    }
    nova_puts("chroot ok");
}

static void sh_rm(const char *line, int dir) {
    const char *path = sh_arg(line);
    int32_t r;
    if (*path == 0) {
        nova_puts(dir ? "usage: rmdir <dir>" : "usage: rm <path>");
        return;
    }
    r = dir ? nova_rmdir(path) : nova_unlink(path);
    if (r == -NOVA_EACCES) {
        sh_denied(dir ? "rmdir" : "rm");
        return;
    }
    if (r != 0) {
        sh_err(dir ? "rmdir" : "rm", r);
        return;
    }
    nova_puts("ok");
}

static void sh_uloop(const char *line) {
    /* uloop <port> <text>: bind, loop text through 127.0.0.1 to
     * self, print what comes back. One command proves send +
     * receive without a second terminal. */
    const char *p = sh_arg(line);
    int port = 0;
    int32_t id;
    int32_t r;
    struct nova_sock_send sargs;
    struct nova_sock_recv rargs;
    static char rxb[512];
    p = sh_nextnum(p, &port);
    if (p == 0 || port <= 0 || port > 65535) {
        nova_puts("usage: uloop <port> <text>");
        return;
    }
    while (*p == ' ') {
        p++;
    }
    if (*p == 0) {
        nova_puts("usage: uloop <port> <text>");
        return;
    }
    id = nova_sock(SOCK_OP_OPEN, (uint32_t)port, 0);
    if (id < 0) {
        sh_err("uloop", id);
        return;
    }
    sargs.dst = 0x7F000001u;
    sargs.dport = (uint16_t)port;
    sargs._pad[0] = 0;
    sargs._pad[1] = 0;
    sargs.buf = (uint32_t)p;
    sargs.len = nova_strlen(p);
    r = nova_sock(SOCK_OP_SEND, (uint32_t)id, (uint32_t)&sargs);
    if (r != 0) {
        nova_sock(SOCK_OP_CLOSE, (uint32_t)id, 0);
        sh_err("uloop", r);
        return;
    }
    rargs.buf = (uint32_t)rxb;
    rargs.max = sizeof(rxb);
    rargs.timeout = 200;
    r = nova_sock(SOCK_OP_RECV, (uint32_t)id, (uint32_t)&rargs);
    nova_sock(SOCK_OP_CLOSE, (uint32_t)id, 0);
    if (r < 0) {
        sh_err("uloop", r);
        return;
    }
    nova_print("got ", 4);
    sh_putstrn(rxb, (uint32_t)r);
    nova_putchar('\n');
}

static void sh_nslookup(const char *line) {
    const char *name = sh_arg(line);
    uint32_t ip = 0;
    int32_t r;
    if (*name == 0) {
        nova_puts("usage: nslookup <name>");
        return;
    }
    r = nova_dns(name, &ip);
    if (r != 0) {
        sh_err("nslookup", r);
        return;
    }
    sh_putstrn(name, 64);
    nova_print(" has ", 5);
    sh_putip(ip);
    nova_putchar('\n');
}

static const char *sh_nextnum(const char *p, int *val) {
    char tok[12];
    uint32_t i = 0;
    int ok = 0;
    while (*p == ' ') {
        p++;
    }
    while (*p != 0 && *p != ' ' && i + 1u < sizeof(tok)) {
        tok[i++] = *p++;
    }
    tok[i] = 0;
    if (i == 0 || (*p != 0 && *p != ' ')) {
        return 0;
    }
    *val = nova_strtoi(tok, &ok);
    if (!ok) {
        return 0;
    }
    return p;
}

static void sh_putstrn(const char *s, uint32_t max) {
    uint32_t i = 0;
    while (i < max && s[i] != 0) {
        nova_putchar(s[i]);
        i++;
    }
}

static void sh_focus(void) {
    int32_t r = nova_winfocus();
    if (r < 0) {
        nova_puts("no windows");
        return;
    }
    nova_print("focus id=", 9);
    sh_putdec((uint32_t)r);
    nova_putchar('\n');
}

static void sh_stat(void) {
    static struct nova_stats st;
    int32_t r = nova_stats(&st);
    if (r != 0) {
        sh_err("stat", r);
        return;
    }
    nova_print("ticks=", 6);
    sh_putdec(st.ticks);
    nova_print(" switches=", 10);
    sh_putdec(st.switches);
    nova_putchar('\n');
    nova_print("irqs=", 5);
    sh_putdec(st.irqs);
    nova_print(" threads=", 9);
    sh_putdec(st.threads);
    nova_putchar('\n');
    nova_print("pmm_free=", 9);
    sh_putdec(st.pmm_free);
    nova_putchar('\n');
}

static void sh_wins(void) {
    static struct nova_winstat ws[8];
    int32_t n = nova_winstat(ws, 8);
    int32_t i;
    if (n < 0) {
        sh_err("wins", n);
        return;
    }
    if (n == 0) {
        nova_puts("no windows");
        return;
    }
    for (i = 0; i < n && i < 8; i++) {
        sh_putdec(ws[i].id);
        nova_putchar(' ');
        sh_putdec(ws[i].x);
        nova_putchar(',');
        sh_putdec(ws[i].y);
        nova_putchar(' ');
        sh_putdec(ws[i].w);
        nova_putchar('x');
        sh_putdec(ws[i].h);
        nova_putchar(' ');
        if (ws[i].focused != 0) {
            nova_print("* ", 2);
        }
        sh_putstrn(ws[i].title, 16);
        nova_putchar('\n');
    }
}

static void sh_winopen(const char *line) {
    const char *p = sh_arg(line);
    int v[5];
    struct nova_winopen args;
    int32_t r;
    for (int k = 0; k < 5; k++) {
        p = sh_nextnum(p, &v[k]);
        if (p == 0 || v[k] < 0) {
            nova_puts("usage: winopen x y w h color");
            return;
        }
    }
    while (*p == ' ') {
        p++;
    }
    if (*p != 0 || v[4] > 15) {
        nova_puts("usage: winopen x y w h color");
        return;
    }
    args.x = (uint32_t)v[0];
    args.y = (uint32_t)v[1];
    args.w = (uint32_t)v[2];
    args.h = (uint32_t)v[3];
    args.color = (uint8_t)v[4];
    args._pad[0] = 0;
    args._pad[1] = 0;
    args._pad[2] = 0;
    r = nova_winopen(&args);
    if (r < 0) {
        sh_err("winopen", r);
        return;
    }
    nova_print("win id=", 7);
    sh_putdec((uint32_t)r);
    nova_putchar('\n');
}

static void sh_winclose(const char *line) {
    const char *p = sh_arg(line);
    int id = 0;
    int32_t r;
    p = sh_nextnum(p, &id);
    if (p == 0 || id < 0) {
        nova_puts("usage: winclose id");
        return;
    }
    r = nova_winclose((uint32_t)id);
    if (r != 0) {
        sh_err("winclose", r);
        return;
    }
    nova_puts("winclose ok");
}

static void sh_click(const char *line) {
    const char *p = sh_arg(line);
    int v[2];
    int32_t r;
    for (int k = 0; k < 2; k++) {
        p = sh_nextnum(p, &v[k]);
        if (p == 0 || v[k] < 0) {
            nova_puts("usage: click x y");
            return;
        }
    }
    while (*p == ' ') {
        p++;
    }
    if (*p != 0) {
        nova_puts("usage: click x y");
        return;
    }
    r = nova_winclick((uint32_t)v[0], (uint32_t)v[1]);
    if (r < 0) {
        nova_puts("click: no window");
        return;
    }
    nova_print("click id=", 9);
    sh_putdec((uint32_t)r);
    nova_putchar('\n');
}

static void sh_wintitle(const char *line) {
    const char *p = sh_arg(line);
    int id = 0;
    int32_t r;
    p = sh_nextnum(p, &id);
    if (p == 0 || id < 0) {
        nova_puts("usage: wintitle id text");
        return;
    }
    while (*p == ' ') {
        p++;
    }
    if (*p == 0) {
        nova_puts("usage: wintitle id text");
        return;
    }
    r = nova_wintitle((uint32_t)id, p);
    if (r != 0) {
        sh_err("wintitle", r);
        return;
    }
    nova_puts("wintitle ok");
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
    int32_t r = nova_meminfo(&total, &freep);
    if (r != 0) {
        sh_err("meminfo", r);
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

static void sh_uptime(void) {
    /* Ticks are 100 Hz nominal (TIMEBASE.md); seconds are
     * approximate by construction, and labeled as such. */
    int32_t t = nova_ticks();
    uint32_t u;
    if (t < 0) {
        sh_err("uptime", t);
        return;
    }
    u = (uint32_t)t;
    nova_print("uptime ticks=", 13);
    sh_putdec(u);
    nova_print(" (~", 3);
    sh_putdec(u / 100u);
    nova_puts(" s @100Hz nominal)");
}

static void sh_about(void) {
    /* Identity from real sources only: build version
     * string, ABI number from the kernel, and the one
     * architecture this binary targets. */
    int32_t abi = nova_version();
    nova_print("nova sh ", 8);
    nova_print(NOVA_VERSION, nova_strlen(NOVA_VERSION));
    nova_puts(" (x86-32 BIOS prototype)");
    if (abi < 0) {
        sh_err("abi", abi);
        return;
    }
    nova_print("abi=", 4);
    sh_putdec((uint32_t)abi);
    nova_putchar('\n');
}

static void sh_rt(const char *line) {
    /* rt [tid]: per-thread runtime in ticks (0 = caller). */
    static struct nova_runtime r;
    const char *p = sh_arg(line);
    int ok = 0;
    int tid = 0;
    int32_t rc;
    if (*p != 0) {
        tid = nova_strtoi(p, &ok);
        if (!ok || tid < 0) {
            nova_puts("usage: rt [tid]");
            return;
        }
    }
    rc = nova_runtime((uint32_t)tid, &r);
    if (rc < 0) {
        nova_puts("rt: no such thread");
        return;
    }
    nova_print("tid=", 4);
    sh_putdec(r.tid);
    nova_print(" runtime_ticks=", 15);
    sh_putdec(r.runtime);
    nova_print(" state=", 7);
    sh_putdec(r.state);
    nova_print(" prio=", 6);
    sh_putdec(r.prio);
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

/* Opaque-failure reporter (Phase K): prints the errno number
 * so shell errors stay classifiable (EACCES denials keep
 * their friendlier text above; specific cases likewise). */
static void sh_err(const char *what, int32_t rc) {
    uint32_t e = (uint32_t)(rc < 0 ? -rc : rc);
    nova_print(what, nova_strlen(what));
    nova_print(" failed E", 9);
    sh_putdec(e);
    nova_putchar('\n');
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
                sh_err("ls", rc);
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
        } else if (fd == -2) { /* -ENOENT (no SDK macro; cf. sh_ls) */
            nova_puts("cat: no such file");
        } else {
            sh_err("cat", fd);
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
        sh_err("mkdir", r);
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
            sh_err("put: open", fd);
        }
        return;
    }
    n = nova_strlen(text);
    {
        int32_t w = nova_write(fd, text, n);
        if (w < 0) {
            sh_err("put: write", w);
        } else if (w != (int32_t)n) {
            nova_puts("put: short write");
        }
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
        sh_err("fsck", r);
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
                /* Saturate the accumulator (Phase J): overlong
                 * runs must refuse via n/val checks below, never
                 * via signed overflow (UB even when harmless). */
                if (n < 9) {
                    val = val * 10 + (*p - '0');
                }
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
            sh_err("ping", r);
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
        sh_err("run", pid);
        return;
    }
    nova_print("child pid=", 10);
    sh_putdec((uint32_t)pid);
    nova_putchar('\n');
}

static void sh_run(const char *line) {
    /* Repeat-last (Track B): "!!" re-runs the stored line.
     * Stored lines are never "!!" (expansion is not stored),
     * so this cannot recurse. */
    static char sh_last[128];
    if (line[0] == 0) {
        return;
    } else if (line[0] == '!' && line[1] == '!' && line[2] == 0) {
        if (sh_last[0] == 0) {
            nova_puts("no history");
            return;
        }
        line = sh_last;
    } else {
        uint32_t i = 0;
        while (line[i] != 0 && i + 1u < sizeof(sh_last)) {
            sh_last[i] = line[i];
            i++;
        }
        sh_last[i] = 0;
    }
    if (nova_strncmp(line, "help", 4) == 0 &&
               (line[4] == 0 || line[4] == ' ')) {
        sh_help(line);
    } else if (nova_strncmp(line, "echo", 4) == 0) {
        sh_echo(line);
    } else if (nova_strcmp(line, "mem") == 0) {
        sh_mem();
    } else if (nova_strcmp(line, "ticks") == 0) {
        sh_ticks();
    } else if (nova_strcmp(line, "uptime") == 0) {
        sh_uptime();
    } else if (nova_strcmp(line, "about") == 0) {
        sh_about();
    } else if (nova_strncmp(line, "rt", 2) == 0 &&
               (line[2] == 0 || line[2] == ' ')) {
        sh_rt(line);
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
    } else if (nova_strcmp(line, "focus") == 0) {
        sh_focus();
    } else if (nova_strcmp(line, "wins") == 0) {
        sh_wins();
    } else if (nova_strncmp(line, "winopen", 7) == 0 &&
               (line[7] == 0 || line[7] == ' ')) {
        sh_winopen(line);
    } else if (nova_strncmp(line, "winclose", 8) == 0 &&
               (line[8] == 0 || line[8] == ' ')) {
        sh_winclose(line);
    } else if (nova_strncmp(line, "wintitle", 8) == 0 &&
               (line[8] == 0 || line[8] == ' ')) {
        sh_wintitle(line);
    } else if (nova_strncmp(line, "click", 5) == 0 &&
               (line[5] == 0 || line[5] == ' ')) {
        sh_click(line);
    } else if (nova_strncmp(line, "rmdir", 5) == 0 &&
               (line[5] == 0 || line[5] == ' ')) {
        sh_rm(line, 1);
    } else if (nova_strncmp(line, "uloop", 5) == 0 &&
               (line[5] == 0 || line[5] == ' ')) {
        sh_uloop(line);
    } else if (nova_strncmp(line, "nslookup", 8) == 0 &&
               (line[8] == 0 || line[8] == ' ')) {
        sh_nslookup(line);
    } else if (nova_strcmp(line, "stat") == 0) {
        sh_stat();
    } else if (nova_strncmp(line, "rm", 2) == 0 &&
               (line[2] == 0 || line[2] == ' ')) {
        sh_rm(line, 0);
    } else if (nova_strcmp(line, "exit") == 0) {
        /* Ends the session: the shell thread dies here and there
         * is no respawn in this beta (no process reaping), so the
         * recovery instruction must print BEFORE exiting. */
        nova_puts("bye; reset the VM for a new shell");
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
