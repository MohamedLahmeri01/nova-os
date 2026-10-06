"""Corpus generator (Phase 0.3-F wire seeds, Phase 0.4 shell).

Rebuilds tests/corpus/** byte-for-byte:
- DHCP + DNS wire seeds (from tests/unit/test_net.c constructions).
- Shell lines (boundary lengths, overflow numbers, weird bytes).
Run from repo root: python tools/mkcorpus.py
Any replay failure afterwards means the files drifted: regenerate.
"""
import os

OUT = os.path.join("tests", "corpus")
os.makedirs(OUT, exist_ok=True)

TMAC = bytes([0x08, 0x00, 0x27, 0x3C, 0xA1, 0xA1])


def dhcp_base():
    m = bytearray(300)
    m[0] = 2
    m[1] = 1
    m[2] = 6
    m[4:8] = bytes([0x12, 0x34, 0x56, 0x78])
    m[28:34] = TMAC
    m[16] = 10
    m[18] = 2
    m[19] = 15
    m[236:240] = bytes([99, 130, 83, 99])
    opts = bytes([53, 1, 2,
                  54, 4, 10, 0, 2, 2,
                  1, 4, 255, 255, 255, 0,
                  3, 4, 10, 0, 2, 2,
                  51, 4, 0, 0, 0x0E, 0x10,
                  255])
    m[240:240 + len(opts)] = opts
    return m


def w(name, data):
    p = os.path.join(OUT, name)
    with open(p, "wb") as f:
        f.write(bytes(data))
    print(f"{name}: {len(data)}B")


base = dhcp_base()
w("dhcp-offer.bin", base)
m = bytearray(base)
m[4] ^= 1
w("dhcp-xid.bin", m)
m = bytearray(base)
m[236] = 0
w("dhcp-magic.bin", m)
w("dhcp-short.bin", base[:200])
m = bytearray(base)
m[28] ^= 1
w("dhcp-chaddr.bin", m)
m = bytearray(base)
m[19] = 0
w("dhcp-wireaddr.bin", m)

q = bytes([0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00,
           0x00, 0x00, 0x00, 0x00, 0x07]) + b"example" + \
    bytes([0x03]) + b"com" + bytes([0x00, 0x00, 0x01, 0x00, 0x01])
assert len(q) == 29
ans = bytes([0xC0, 0x0C, 0x00, 0x01, 0x00, 0x01,
             0x00, 0x00, 0x00, 0x3C, 0x00, 0x04,
             93, 184, 216, 34])


def reply(mut=None, cut=None):
    r = bytearray(bytes([0x12, 0x34, 0x81, 0x80, 0x00, 0x01,
                         0x00, 0x01, 0x00, 0x00, 0x00, 0x00]) +
                  q[12:] + ans)
    if mut:
        for off, val in mut:
            r[off] = val
    if cut is not None:
        r = r[:cut]
    return r


w("dns-ok.bin", reply())
m = reply()
m[0] ^= 1
w("dns-id.bin", m)
m = reply()
m[3] = 0x83
w("dns-nxdomain.bin", m)
w("dns-trunc.bin", reply(cut=len(reply()) - 5))
m = reply()
m[29 + 2] = 0x00
m[29 + 3] = 0x05
w("dns-cname.bin", m)

SH = os.path.join(OUT, "shell")
os.makedirs(SH, exist_ok=True)


def t(name, data):
    if isinstance(data, str):
        data = data.encode("latin-1")
    p = os.path.join(SH, name)
    with open(p, "wb") as f:
        f.write(data)
    print(f"shell/{name}: {len(data)}B")


t("shell-01-empty.txt", b"")
t("shell-02-spaces.txt", b"   ")
t("shell-03-help.txt", b"help")
t("shell-04-unknown.txt", b"frobnicate --xyz")
t("shell-05-a127.txt", b"a" * 127)
t("shell-06-a128.txt", b"a" * 128)
t("shell-07-a300.txt", b"a" * 300)
t("shell-08-winopen-ok.txt", b"winopen 8 16 190 176 1")
t("shell-09-winopen-huge.txt", b"winopen 9999999999 0 0 0 0")
t("shell-10-winopen-neg.txt", b"winopen -1 -2 3 4 5")
t("shell-11-chmod-bad.txt", b"chmod 888 /x")
t("shell-12-chmod-ok.txt", b"chmod 644 /etc/passwd")
t("shell-13-quote.txt", b"echo \"a'b\" \\c \t tail\x80\xff")
t("shell-14-click-neg.txt", b"click -5 300")
t("shell-15-nslookup-empty.txt", b"nslookup ")
t("shell-16-put-deep.txt", b"put /a/b/c/d/e/f/g/h/i/j/k/l/m/n hello")
print("corpus done")
