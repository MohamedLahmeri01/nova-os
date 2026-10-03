#!/usr/bin/env python3
"""NOVA OS native filesystem fixture generator (Phase 7f-1).

Builds a deterministic NOVA-FS v0.1 image per docs/NOVA-FS.md: 4KB
blocks, 128 blocks, 16 inodes, inline extents, metadata journal area
(zeroed; reader ignores contents), redundant superblock, crc32c
checksums over superblock bytes 0..47 and inode bytes 0..111.

Contents (CONTRACT: kernel nova_selftest asserts these exact bytes):
  /HELLO.TXT      "hello from nova\\n"  (16B, inode 4, block 69)
  /DOCS/NOTE.TXT  "nova nested ok\\n"   (15B, inode 5, block 71)

Fixed UUID/label/timestamps: rebuilding yields byte-identical output.
"""
import struct
import sys

BLOCK = 4096
NBLOCKS = 128
NINODES = 16
MTIME = 1725148800  # fixed, deterministic
HELLO = b"hello from nova\n"
NOTE = b"nova nested ok\n"
VOL = b"NOVAFS"
UUID = b"NOVAFSIMG0000001"  # 16B
JSTART = 1
JCOUNT = 64


def _crc_table():
    t = []
    for i in range(256):
        c = i
        for _ in range(8):
            c = (c >> 1) ^ (0x82F63B78 if c & 1 else 0)
        t.append(c)
    return t


_CRC = _crc_table()


def crc32c(data):
    crc = 0xFFFFFFFF
    for b in data:
        crc = _CRC[(crc ^ b) & 0xFF] ^ (crc >> 8)
    return crc ^ 0xFFFFFFFF


def dirent(ino, name, ftype):
    n = len(name)
    reclen = ((8 + n + 3) // 4) * 4
    e = bytearray(reclen)
    struct.pack_into("<IHBB", e, 0, ino, reclen, n, ftype)
    e[8:8 + n] = name
    return bytes(e)


def superblock(free_blocks):
    sb = bytearray(1024)
    sb[0:8] = b"NOVAFS01"
    struct.pack_into("<I", sb, 8, BLOCK)
    struct.pack_into("<I", sb, 12, NBLOCKS)
    struct.pack_into("<I", sb, 16, free_blocks)
    struct.pack_into("<I", sb, 20, NINODES)
    struct.pack_into("<I", sb, 24, NINODES - 5)
    struct.pack_into("<I", sb, 28, 2)  # root inode
    struct.pack_into("<I", sb, 32, JSTART)
    struct.pack_into("<I", sb, 36, JCOUNT)
    struct.pack_into("<I", sb, 40, 0)  # journal sequence
    struct.pack_into("<I", sb, 44, 0)  # features
    sb[64:80] = UUID
    sb[80:96] = VOL + b"\x00" * (16 - len(VOL))
    struct.pack_into("<I", sb, 96, 67)  # inode table
    struct.pack_into("<I", sb, 100, 65)  # block bitmap
    struct.pack_into("<I", sb, 104, 66)  # inode bitmap
    struct.pack_into("<I", sb, 48, crc32c(bytes(sb[0:48])))
    return bytes(sb)


def inode(mode, size, links, ext_start):
    ino = bytearray(128)
    struct.pack_into("<H", ino, 0, mode)
    struct.pack_into("<I", ino, 4, size)
    struct.pack_into("<I", ino, 8, MTIME)
    struct.pack_into("<I", ino, 12, MTIME)
    struct.pack_into("<I", ino, 16, MTIME)
    struct.pack_into("<H", ino, 26, links)
    struct.pack_into("<I", ino, 28, 8)  # 512B units for 1 block
    # extent root: magic, count=1, depth=0, reserved
    struct.pack_into("<HHHH", ino, 40, 0xF30A, 1, 0, 0)
    struct.pack_into("<IHH", ino, 48, ext_start, 1, 0)
    struct.pack_into("<I", ino, 112, crc32c(bytes(ino[0:112])))
    return bytes(ino)


USED = ([0] + list(range(JSTART, JSTART + JCOUNT)) +
        [65, 66, 67, 68, 69, 70, 71, NBLOCKS - 1])


def dirblock(entries, slack):
    """Dir block image: entries + `slack` empty 32B slots (ino=0),
    last slot stretched to end of block (scan terminator rule)."""
    blk = bytearray(BLOCK)
    at = 0
    for ino, nm, ft in entries:
        e = dirent(ino, nm, ft)
        blk[at:at + len(e)] = e
        at += len(e)
    for _ in range(slack):
        struct.pack_into("<IHBB", blk, at, 0, 32, 0, 0)
        at += 32
    struct.pack_into("<H", blk, at - 32 + 4, BLOCK - (at - 32))
    return bytes(blk)


def build():
    img = bytearray(NBLOCKS * BLOCK)
    free = NBLOCKS - len(USED)

    sb = superblock(free)
    img[1024:2048] = sb  # copy 0 in block 0
    img[(NBLOCKS - 1) * BLOCK + 1024:(NBLOCKS - 1) * BLOCK + 2048] = sb

    # --- block bitmap (block 65): USED bits set ---
    for b in USED:
        img[65 * BLOCK + b // 8] |= (1 << (b % 8))

    # --- inode bitmap (block 66): bit 0 reserved, inodes 2,3,4,5 ---
    img[66 * BLOCK] = 0x1F

    # --- inode table (block 67) ---
    o = 67 * BLOCK
    img[o + 128:o + 256] = inode(0x41ED, BLOCK, 3, 68)  # root
    img[o + 256:o + 384] = inode(0x41ED, BLOCK, 2, 70)  # DOCS
    img[o + 384:o + 512] = inode(0x81A4, len(HELLO), 1, 69)
    img[o + 512:o + 640] = inode(0x81A4, len(NOTE), 1, 71)

    # --- root dir (block 68): 4 spare slots for create tests ---
    img[68 * BLOCK:69 * BLOCK] = dirblock(
        ((2, b".", 2), (2, b"..", 2), (4, b"HELLO.TXT", 1),
         (3, b"DOCS", 2)),
        4)

    # --- file data ---
    img[69 * BLOCK:69 * BLOCK + len(HELLO)] = HELLO
    img[71 * BLOCK:71 * BLOCK + len(NOTE)] = NOTE

    # --- DOCS dir (block 70): 2 spare slots ---
    img[70 * BLOCK:71 * BLOCK] = dirblock(
        ((3, b".", 2), (2, b"..", 2), (5, b"NOTE.TXT", 1)), 2)

    return bytes(img)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "images/nova.raw"
    img = build()
    with open(out, "wb") as f:
        f.write(img)
    print(f"[ok] nova-fs image {len(img)}B -> {out}")


if __name__ == "__main__":
    main()
