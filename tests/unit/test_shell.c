/* NOVA OS shell parser fuzz (Phase 0.4-M, T23).
 * Host-side: the REAL userspace/init/sh.c (plus libc string/
 * stdio) with syscalls neutered at the macro level: every
 * nova_syscall(...) call site expands to the test stub, so no
 * INT 0x80 ever executes. PRINT output is captured bounded;
 * all other calls fail closed (-EIO) without touching memory,
 * which drives every builtin down its parse/error paths.
 * Matrix: exact lines (bounds, overflow, weird bytes), corpus
 * replay (tests/corpus shell text files via argv[1]), strtoi unit
 * vectors (incl. the overflow UB this phase fixed), then 3000
 * PRNG soup lines. Every line must return with bounded output
 * (parsers advance or refuse; hangs would hang the suite, and
 * none exist by construction — verified by this run).
 * VERDICT line.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/* Syscall neutering via the header seam (NOVA_TEST_STUB_SYSCALL
 * on the compile line): every nova_syscall(...) site expands to
 * the stub below, so no INT 0x80 ever executes. PRINT output is
 * captured bounded; all other calls fail closed (-EIO) without
 * touching memory, driving every builtin down parse/error paths.
 */

/* Bounded output capture (PRINT writes only). Sized for the
 * whole battery (per-line bound enforced separately). */
static uint8_t cap_buf[262144];
static uint32_t cap_n;
static int cap_overflow;
static int stub_calls;

/* Non-static: matches the header seam declaration (the macro
 * renames call sites to this definition). */
int32_t test_syscall_stub(uint32_t nr, uint32_t a, uint32_t b,
                          uint32_t c) {
    (void)c;
    stub_calls++;
    if (nr == 2u) { /* SYS_PRINT: capture, bounded */
        const uint8_t *p = (const uint8_t *)(uintptr_t)a;
        uint32_t i;
        for (i = 0; i < b; i++) {
            if (cap_n >= sizeof(cap_buf)) {
                cap_overflow = 1;
                break;
            }
            cap_buf[cap_n++] = p[i];
        }
        return (int32_t)b;
    }
    /* Phase N.2: succeed the read-only identity/time calls
     * with fixed values so success paths (not just error
     * paths) are covered; everything else still fails
     * closed. SYS_TICKS=8 -> 12345 ticks, SYS_VERSION=5
     * -> ABI 1. */
    if (nr == 8u) {
        return 12345;
    }
    if (nr == 5u) {
        return 1;
    }
    return -5; /* EIO: fail closed, touch nothing */
}

#include "libs/libc/string.c"
#include "libs/libc/stdio.c"

/* nova_exit() never returns on guest (the thread dies), so
 * nova.h spins for(;;) after the syscall — which would hang
 * the host harness where the stub returns. Redirect the call
 * sites in sh.c to a recording stub (test-only; production
 * code untouched). Must precede the sh.c include. */
static int exit_calls;

static void test_nova_exit(int code) {
    (void)code;
    exit_calls++;
}

#define nova_exit(code) test_nova_exit(code)

#include "init/sh.c"

static int g_failed;

static void check(int cond, const char *what, unsigned long a,
                  unsigned long b) {
    if (!cond) {
        printf("FAIL: %s (%lu, %lu)\n", what, a, b);
        g_failed = 1;
    }
}

/* One line through dispatch: must return, output stays bounded. */
static void runline(const char *line, const char *name) {
    uint32_t before = cap_n;
    sh_run(line);
    check(cap_n - before < 4096u, name, cap_n - before, 0);
}

/* Output-content probe (state-reset evidence: a valid command
 * after garbage must still produce its full text). */
static int cap_contains(uint32_t from, const char *needle) {
    uint32_t nl = 0;
    uint32_t i;
    while (needle[nl] != 0) {
        nl++;
    }
    for (i = from; i + nl <= cap_n; i++) {
        uint32_t k = 0;
        while (k < nl && cap_buf[i + k] == (uint8_t)needle[k]) {
            k++;
        }
        if (k == nl) {
            return 1;
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    const char *cdir = (argc > 1) ? argv[1] : "tests/corpus/shell";
    static const char *files[] = {
        "shell-01-empty.txt", "shell-02-spaces.txt",
        "shell-03-help.txt", "shell-04-unknown.txt",
        "shell-05-a127.txt", "shell-06-a128.txt",
        "shell-07-a300.txt", "shell-08-winopen-ok.txt",
        "shell-09-winopen-huge.txt", "shell-10-winopen-neg.txt",
        "shell-11-chmod-bad.txt", "shell-12-chmod-ok.txt",
        "shell-13-quote.txt", "shell-14-click-neg.txt",
        "shell-15-nslookup-empty.txt", "shell-16-put-deep.txt",
        "shell-17-ping-huge.txt", "shell-18-exit-args.txt",
        "shell-19-plus-num.txt", "shell-20-ctrl.txt",
    };
    uint32_t i;
    uint32_t lcg = 0x5EED01u;

    /* strtoi unit vectors (overflow UB fixed this phase). */
    {
        int ok = 0;
        check(nova_strtoi("123", &ok) == 123 && ok, "stoi-ok",
              0, 0);
        check(nova_strtoi("-42", &ok) == -42 && ok, "stoi-neg",
              0, 0);
        check(nova_strtoi("", &ok) == 0 && !ok, "stoi-empty",
              0, 0);
        check(nova_strtoi("12a", &ok) == 0 && !ok, "stoi-garbage",
              0, 0);
        check(nova_strtoi("9999999999", &ok) == 0 && !ok,
              "stoi-overflow", 0, 0);
        check(nova_strtoi("2147483647", &ok) == 2147483647 && ok,
              "stoi-max", 0, 0);
        check(nova_strtoi("2147483648", &ok) == 0 && !ok,
              "stoi-maxplus", 0, 0);
        check(nova_strtoi("-2147483648", &ok) == 0 && !ok,
              "stoi-min", 0, 0);
    }

    /* Exact matrix (bounds that matter). */
    runline("", "empty");
    /* Phase N.2 Track B: history starts empty (must run
     * before any other storing line). */
    {
        uint32_t mark = cap_n;
        runline("!!", "hist-empty");
        check(cap_contains(mark, "no history"), "hist-empty-out",
              0, 0);
    }
    /* Phase N.2 Track A: table<->dispatch consistency.
     * Every table name must dispatch to something real
     * (never the unknown message). */
    {
        static const char *names[] = {
            "help", "echo", "mem", "ticks", "uptime", "about",
            "rt", "ls", "cat", "mkdir", "clear", "run", "put",
            "fsck", "ping", "ip", "dhcp", "caps", "dropcap",
            "rlimit", "id", "setuid", "chmod", "chroot",
            "focus", "wins", "winopen", "winclose", "wintitle",
            "click", "rmdir", "uloop", "nslookup", "stat", "rm",
            "!!", "exit",
        };
        uint32_t k;
        for (k = 0; k < sizeof(names) / sizeof(names[0]); k++) {
            uint32_t mark = cap_n;
            runline(names[k], "dispatch-table");
            check(!cap_contains(mark, "unknown: try help"),
                  "dispatch-real", k, 0);
        }
        /* "exit" ran once above: the exit builtin must have
         * attempted exit exactly once (recording stub). */
        check(exit_calls == 1, "dispatch-exit-once", exit_calls, 0);
    }
    /* Phase N.2 Track A: bare names flagged bare_usage
     * must print usage (verified per handler). */
    {
        static const char *bare[] = {
            "cat", "mkdir", "run", "put", "dropcap", "setuid",
            "chmod", "chroot", "rm", "rmdir", "uloop",
            "nslookup", "winopen", "winclose", "click",
            "wintitle",
        };
        uint32_t k;
        for (k = 0; k < sizeof(bare) / sizeof(bare[0]); k++) {
            uint32_t mark = cap_n;
            runline(bare[k], "bare-usage");
            check(cap_contains(mark, "usage:"), "bare-usage-out",
                  k, 0);
        }
    }
    /* Phase N.2 Track A: help lists every name; help <cmd>
     * describes one; help <bogus> stays unknown. */
    {
        uint32_t mark = cap_n;
        runline("help", "help-list");
        check(cap_contains(mark, "commands:"), "help-head", 0, 0);
        check(cap_contains(mark, "uptime"), "help-uptime", 0, 0);
        check(cap_contains(mark, "about"), "help-about", 0, 0);
        check(cap_contains(mark, "!!"), "help-hist", 0, 0);
        mark = cap_n;
        runline("help cat", "help-one");
        check(cap_contains(mark, "usage: cat <path>"),
              "help-one-out", 0, 0);
        mark = cap_n;
        runline("help stat", "help-noargs");
        check(cap_contains(mark, "(no arguments)"),
              "help-noargs-out", 0, 0);
        mark = cap_n;
        runline("help bogus", "help-bogus");
        check(cap_contains(mark, "unknown: try help"),
              "help-bogus-out", 0, 0);
    }
    /* Phase N.2 Track B: !! repeats the last stored line. */
    {
        uint32_t mark;
        runline("ticks", "hist-store");
        mark = cap_n;
        runline("!!", "hist-repeat");
        check(cap_contains(mark, "ticks="), "hist-repeat-out",
              0, 0);
    }
    /* Phase N.2 Track C: uptime/about from real sources
     * (stub ticks=12345, ABI=1). */
    {
        uint32_t mark = cap_n;
        runline("uptime", "uptime");
        check(cap_contains(mark, "uptime ticks=12345"),
              "uptime-out", 0, 0);
        check(cap_contains(mark, "~123 s @100Hz nominal"),
              "uptime-units", 0, 0);
        mark = cap_n;
        runline("about", "about");
        check(cap_contains(mark, "nova sh "), "about-name",
              0, 0);
        check(cap_contains(mark, "x86-32 BIOS prototype"),
              "about-arch", 0, 0);
        check(cap_contains(mark, "abi=1"), "about-abi", 0, 0);
    }
    runline("   ", "spaces");
    runline("help", "help");
    {
        char a300[301];
        for (i = 0; i < 300u; i++) {
            a300[i] = 'a';
        }
        a300[300] = 0;
        a300[127] = 0;
        runline(a300, "a127");
        a300[127] = 'a';
        a300[128] = 0;
        runline(a300, "a128");
        a300[128] = 'a';
        a300[300] = 0;
        runline(a300, "a300");
    }
    runline("winopen 9999999999 0 0 0 0", "huge-num");
    runline("ping 9999999999", "ping-huge");
    runline("ping 1.2.3.99999", "ping-oct-huge");

    /* Dispatch confusion: prefixes/suffixes must resolve
     * deterministically (exact match or prefix+boundary;
     * never another command, never a crash). */
    runline("r", "pfx-r");
    runline("ru", "pfx-ru");
    runline("rmdi", "pfx-rmdi");
    runline("winsx", "pfx-winsx");
    runline("statx", "pfx-statx");
    runline("exit now", "exit-args");
    runline("ticks1", "ticks-suffix");
    runline("clear x", "clear-args");
    runline("help me", "help-args");
    runline("echox", "echo-nospace");

    /* Argument count/shape (every builtin with args). */
    runline("winopen 1 2 3", "winopen-short");
    runline("winopen 1 2 3 4 5 6", "winopen-long");
    runline("winclose", "winclose-empty");
    runline("wintitle 1", "wintitle-short");
    runline("click 1", "click-short");
    runline("click 1 2 3", "click-long");
    runline("chmod 644", "chmod-short");
    runline("chmod 644444 /x", "chmod-longmode");
    runline("put onlypath", "put-short");
    runline("rt -1", "rt-neg");
    runline("rt abc", "rt-alpha");
    runline("rt 9999999999", "rt-huge");
    runline("dropcap xyz", "dropcap-bad");
    runline("rlimit bogus 1", "rlimit-bad");
    runline("rlimit ticks -5", "rlimit-neg");
    runline("setuid", "setuid-empty");
    runline("setuid +1", "setuid-plus");
    runline("uloop", "uloop-empty");
    runline("uloop abc text", "uloop-alpha");
    runline("run", "run-empty");
    runline("cat", "cat-empty");
    runline("mkdir", "mkdir-empty");
    runline("rm", "rm-empty");
    runline("rmdir", "rmdir-empty");
    runline("chroot", "chroot-empty");
    runline("nslookup", "nslookup-empty");

    /* Numeric edge: plus-sign, 12-digit cap, INT_MAX. */
    runline("winopen 2147483647 0 0 0 0", "num-intmax");
    runline("winopen 123456789012 0 0 0 0", "num-12digit");
    runline("click +1 +2", "num-plus");

    /* State reset: garbage, overflow, then a valid command
     * must still produce its full text (no parser poisoning). */
    {
        uint32_t mark;
        runline("???!@#$%^&*()", "reset-garbage");
        runline("winopen 1 2 3 4 5 6 7 8 9 10 11 12", "reset-long");
        mark = cap_n;
        runline("help", "reset-help");
        check(cap_contains(mark, "commands:"), "reset-content",
              0, 0);
        runline("", "reset-empty");
        mark = cap_n;
        runline("ticks", "reset-ticks");
        check(cap_contains(mark, "ticks="), "reset-ticks-out",
              0, 0);
    }

    /* Checked-in corpus replay (one line per file). */
    for (i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        char path[256];
        FILE *f;
        static char line[512];
        size_t n = 0;
        size_t dl = strlen(cdir);
        size_t nl = strlen(files[i]);
        if (dl + nl + 2u > sizeof(path)) {
            check(0, "corpus-path", i, 0);
            continue;
        }
        memcpy(path, cdir, dl);
        path[dl] = '/';
        memcpy(path + dl + 1u, files[i], nl);
        path[dl + 1u + nl] = 0;
        f = fopen(path, "rb");
        if (f == 0) {
            check(0, "corpus-open", i, 0);
            continue;
        }
        n = fread(line, 1, sizeof(line) - 1u, f);
        fclose(f);
        line[n] = 0;
        /* Trailing newline is file formatting, not input. */
        if (n > 0 && line[n - 1u] == '\n') {
            line[n - 1u] = 0;
        }
        runline(line, files[i]);
    }
    printf("corpus: 20 shell lines replayed\n");

    /* PRNG soup (bytes 1..255, lengths 0..200): dispatch must
     * always return with bounded output. Fixed seed. */
    for (i = 0; i < 3000u; i++) {
        char line[201];
        uint32_t len = 0;
        uint32_t seed = lcg;
        lcg = lcg * 1664525u + 1013904223u;
        len = seed % 201u;
        for (uint32_t k = 0; k < len; k++) {
            lcg = lcg * 1664525u + 1013904223u;
            line[k] = (char)(1u + (lcg >> 8) % 255u);
        }
        line[len] = 0;
        runline(line, "fuzz-soup");
        if (g_failed) {
            break;
        }
    }
    printf("fuzz: 3000 shell lines dispatched\n");
    /* Full-range bytes incl. embedded NULs (0..255): lines
     * truncate at the first NUL by construction; every
     * prefix must still terminate with bounded output. */
    for (i = 0; i < 1000u; i++) {
        char line[201];
        uint32_t len = 0;
        uint32_t seed = lcg;
        lcg = lcg * 1664525u + 1013904223u;
        len = seed % 201u;
        for (uint32_t k = 0; k < len; k++) {
            lcg = lcg * 1664525u + 1013904223u;
            line[k] = (char)((lcg >> 7) % 256u);
        }
        line[len] = 0;
        runline(line, "fuzz-fullrange");
        if (g_failed) {
            break;
        }
    }
    printf("fuzz: 1000 full-range lines dispatched\n");
    check(cap_overflow == 0, "output-bounded", cap_n, 0);
    if (g_failed) {
        printf("VERDICT: shell BROKEN\n");
        return 1;
    }
    printf("VERDICT: shell correct\n");
    return 0;
}
