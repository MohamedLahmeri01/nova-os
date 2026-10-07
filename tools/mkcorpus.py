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
DHCP = os.path.join(OUT, "dhcp")
DNS = os.path.join(OUT, "dns")
os.makedirs(DHCP, exist_ok=True)
os.makedirs(DNS, exist_ok=True)


def wbin(sub, name, data):
    p = os.path.join(OUT, sub, name)
    with open(p, "wb") as f:
        f.write(bytes(data))
    print(f"{sub}/{name}: {len(data)}B")

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
wbin("dhcp", "dhcp-offer.bin", base)
m = bytearray(base)
m[4] ^= 1
wbin("dhcp", "dhcp-xid.bin", m)
m = bytearray(base)
m[236] = 0
wbin("dhcp", "dhcp-magic.bin", m)
wbin("dhcp", "dhcp-short.bin", base[:200])
m = bytearray(base)
m[28] ^= 1
wbin("dhcp", "dhcp-chaddr.bin", m)
m = bytearray(base)
m[19] = 0
wbin("dhcp", "dhcp-wireaddr.bin", m)
m = bytearray(base)
m = m[:-1]
wbin("dhcp", "dhcp-noend.bin", m)
m = bytearray(base)
m[241] = 200
wbin("dhcp", "dhcp-badlen.bin", m)

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


wbin("dns", "dns-ok.bin", reply())
m = reply()
m[0] ^= 1
wbin("dns", "dns-id.bin", m)
m = reply()
m[3] = 0x83
wbin("dns", "dns-nxdomain.bin", m)
wbin("dns", "dns-trunc.bin", reply(cut=len(reply()) - 5))
m = reply()
m[29 + 2] = 0x00
m[29 + 3] = 0x05
wbin("dns", "dns-cname.bin", m)
m = reply()
m[29] = 0xC0
m[29 + 1] = 0x1D
wbin("dns", "dns-ptrloop.bin", m)
second = bytes([0xC0, 0x0C, 0x00, 0x01, 0x00, 0x01,
                0x00, 0x00, 0x00, 0x3C, 0x00, 0x04,
                1, 2, 3, 4])
m = reply()
m[7] = 0x02
wbin("dns", "dns-2ans.bin", m + second)

SH = os.path.join(OUT, "shell")
os.makedirs(SH, exist_ok=True)


def t(name, data):
    if isinstance(data, str):
        data = data.encode("latin-1")
    p = os.path.join(SH, name)
    with open(p, "wb") as f:
        f.write(data)
    print(f"shell/{name}: {len(data)}B")
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
t("shell-17-ping-huge.txt", b"ping 9999999999")
t("shell-18-exit-args.txt", b"exit now")
t("shell-19-plus-num.txt", b"winopen +1 2 3 4 5")
t("shell-20-ctrl.txt", b"\tls\x7f\x80\xff")

UDP = os.path.join(OUT, "udp")
os.makedirs(UDP, exist_ok=True)

MAC_SELF = bytes([0x52, 0x54, 0x00, 0x12, 0x34, 0x56])
MAC_SRC = bytes([0x52, 0x54, 0x00, 0x12, 0x34, 0x57])
IP_SRC = bytes([0x0A, 0x00, 0x02, 0x02])
IP_DST = bytes([0x0A, 0x00, 0x02, 0x0F])


def cksum(data):
    if len(data) % 2:
        data = data + b"\x00"
    s = sum((data[i] << 8) | data[i + 1] for i in range(0, len(data), 2))
    while s > 0xFFFF:
        s = (s & 0xFFFF) + (s >> 16)
    return (~s) & 0xFFFF


def udp_seg(sport, dport, payload, csum=None):
    ulen = 8 + len(payload)
    h = sport.to_bytes(2, "big") + dport.to_bytes(2, "big") + \
        ulen.to_bytes(2, "big") + b"\x00\x00"
    if csum is None:
        pseudo = IP_SRC + IP_DST + bytes([0, 17]) + \
            ulen.to_bytes(2, "big")
        c = cksum(pseudo + h + payload)
        if c == 0:
            c = 0xFFFF
        h = h[:6] + c.to_bytes(2, "big")
    else:
        h = h[:6] + csum.to_bytes(2, "big")
    return h + payload


def ip_pkt(payload, proto=17, ttl=64, frag=0x4000, ver=4):
    total = 20 + len(payload)
    h = bytes([ver << 4 | 5, 0]) + total.to_bytes(2, "big") + \
        bytes([0x12, 0x34]) + frag.to_bytes(2, "big") + \
        bytes([ttl, proto, 0, 0]) + IP_SRC + IP_DST
    c = cksum(h)
    h = h[:10] + c.to_bytes(2, "big") + h[12:]
    return h + payload


def eth_frame(ip, dmac=MAC_SELF):
    return dmac + MAC_SRC + bytes([0x08, 0x00]) + ip


def u(name, frame):
    p = os.path.join(UDP, name)
    with open(p, "wb") as f:
        f.write(frame)
    print(f"udp/{name}: {len(frame)}B")


good_udp = udp_seg(4000, 67, b"Q?")
good_ip = ip_pkt(good_udp)
u("udp-ok.bin", eth_frame(good_ip))  # control: demux drops (port unbound), parses clean
u("udp-hit.bin", eth_frame(ip_pkt(udp_seg(4000, 5200, b"Hi"))))  # accept: bound 5200 delivers (validates generator checksums end to end)
u("ip-badver.bin", eth_frame(ip_pkt(good_udp, ver=5)))
u("ip-mf.bin", eth_frame(ip_pkt(good_udp, frag=0x2000)))
u("ip-ttl0.bin", eth_frame(ip_pkt(good_udp, ttl=0)))
u("ip-trunc.bin", eth_frame(good_ip[:14]))
u("ip-badproto.bin", eth_frame(ip_pkt(good_udp, proto=6)))
u("udp-short.bin", eth_frame(ip_pkt(udp_seg(4000, 67, b"Q?")[:6])))
bad = bytearray(udp_seg(4000, 67, b"Q?"))
bad[8] ^= 0xFF
u("udp-badcksum.bin", eth_frame(ip_pkt(bytes(bad))))
big = udp_seg(4000, 67, b"A" * 100)
big = big[:4] + (8 + 200).to_bytes(2, "big") + big[6:]
u("udp-huge.bin", eth_frame(ip_pkt(big)))
u("eth-multi.bin", eth_frame(ip_pkt(good_udp),
                             dmac=bytes([1, 0, 0x5E, 0, 0, 1])))
u("udp-empty-u.bin", eth_frame(ip_pkt(udp_seg(4000, 67, b""))))
opt_ip = bytearray(ip_pkt(udp_seg(4000, 67, b"Q?")))
opt_ip[0] = 0x46
opt_ip[20:20] = bytes([1, 2, 3, 4])
opt_total = (24 + 8 + 2).to_bytes(2, "big")
opt_ip[2:4] = opt_total
opt_ip[10:12] = b"\x00\x00"
opt_ip[10:12] = cksum(bytes(opt_ip[:24])).to_bytes(2, "big")
u("ip-opt.bin", eth_frame(bytes(opt_ip)))
print("corpus done")
