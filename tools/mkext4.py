#!/usr/bin/env python3
"""NOVA OS EXT4 data-disk generator (Phase 7e).

Builds a deterministic single-group EXT4 image (no journal, no
extents: classic indirect blocks only): 4KB blocks, 32 blocks,
16 inodes, lowercase names (ext4 is case-sensitive).

Contents (CONTRACT: kernel ext_selftest asserts these exact bytes):
  /HELLO.TXT      "hello from ext4\\n"  (16B, inode 4, block 6)
  /DOCS/NOTE.TXT  "ext4 nested ok\\n"   (15B, inode 5, block 8)

Fixed timestamps/UUID: rebuilding yields byte-identical output.
Single block group only (the kernel reader accepts exactly that).
"""
import struct
import sys

BLOCK = 4096
NBLOCKS = 32
NINODES = 16
MTIME = 1725148800  # fixed (2024-09-01), deterministic
HELLO = b"hello from ext4\n"
NOTE = b"ext4 nested ok\n"
VOL = b"NOVAEXT4"
UUID = b"NOVAEXT4DISK0001"


def dirent(ino, name, ftype):
    n = len(name)
    reclen = ((8 + n + 3) // 4) * 4
    e = bytearray(reclen)
    struct.pack_into("<IHBB", e, 0, ino, reclen, n, ftype)
    e[8:8 + n] = name
    return bytes(e)


def inode(mode, size, links, block0):
    ino = bytearray(128)
    struct.pack_into("<H", ino, 0, mode)
    struct.pack_into("<I", ino, 4, size)
    struct.pack_into("<I", ino, 8, MTIME)
    struct.pack_into("<I", ino, 12, MTIME)
    struct.pack_into("<I", ino, 16, MTIME)
    struct.pack_into("<H", ino, 26, links)
    struct.pack_into("<I", ino, 28, 8)  # 512B units for 1 block
    struct.pack_into("<I", ino, 40, block0)
    return bytes(ino)


def build():
    img = bytearray(NBLOCKS * BLOCK)

    # --- superblock at byte 1024 ---
    o = 1024
    struct.pack_into("<I", img, o + 0, NINODES)
    struct.pack_into("<I", img, o + 4, NBLOCKS)
    struct.pack_into("<I", img, o + 12, 23)  # free blocks
    struct.pack_into("<I", img, o + 16, 12)  # free inodes
    struct.pack_into("<I", img, o + 20, 1)  # first data block
    struct.pack_into("<I", img, o + 24, 2)  # 4KB blocks
    struct.pack_into("<I", img, o + 28, 2)
    struct.pack_into("<I", img, o + 32, NBLOCKS)  # per group
    struct.pack_into("<I", img, o + 36, NBLOCKS)
    struct.pack_into("<I", img, o + 40, NINODES)
    struct.pack_into("<I", img, o + 44, MTIME)
    struct.pack_into("<I", img, o + 48, MTIME)
    struct.pack_into("<H", img, o + 54, 20)
    struct.pack_into("<H", img, o + 56, 0xEF53)
    struct.pack_into("<H", img, o + 58, 1)  # clean
    struct.pack_into("<H", img, o + 60, 1)  # errors=continue
    struct.pack_into("<I", img, o + 64, MTIME)
    struct.pack_into("<I", img, o + 72, 0)  # creator Linux
    struct.pack_into("<I", img, o + 76, 1)  # dynamic rev
    struct.pack_into("<I", img, o + 84, 11)  # first ino
    struct.pack_into("<H", img, o + 88, 128)  # inode size
    img[o + 104:o + 120] = UUID
    img[o + 120:o + 136] = VOL + b"\x00" * (16 - len(VOL))

    # --- group descriptor (block 1): bitmaps, inode table ---
    o = BLOCK
    struct.pack_into("<I", img, o + 0, 2)  # block bitmap
    struct.pack_into("<I", img, o + 4, 3)  # inode bitmap
    struct.pack_into("<I", img, o + 8, 4)  # inode table
    struct.pack_into("<H", img, o + 12, 23)  # free blocks
    struct.pack_into("<H", img, o + 14, 12)  # free inodes
    struct.pack_into("<H", img, o + 16, 2)  # dirs count

    # --- block bitmap (block 2): blocks 0..8 used ---
    img[2 * BLOCK] = 0xFF
    img[2 * BLOCK + 1] = 0x01

    # --- inode bitmap (block 3): inodes 2,3,4,5 used ---
    img[3 * BLOCK] = 0x1E

    # --- inode table (block 4) ---
    o = 4 * BLOCK
    img[o + 128:o + 256] = inode(0x41ED, BLOCK, 3, 5)  # root
    img[o + 256:o + 384] = inode(0x41ED, BLOCK, 2, 7)  # DOCS
    img[o + 384:o + 512] = inode(0x81A4, len(HELLO), 1, 6)
    img[o + 512:o + 640] = inode(0x81A4, len(NOTE), 1, 8)

    # --- root dir (block 5) ---
    o = 5 * BLOCK
    at = 0
    for ino, nm, ft in ((2, b".", 2), (2, b"..", 2),
                        (4, b"HELLO.TXT", 1)):
        e = dirent(ino, nm, ft)
        img[o + at:o + at + len(e)] = e
        at += len(e)
    e = dirent(3, b"DOCS", 2)
    img[o + at:o + at + len(e)] = e
    # last entry stretches to end of block
    struct.pack_into("<H", img, o + at + 4, BLOCK - at)

    # --- file data ---
    img[6 * BLOCK:6 * BLOCK + len(HELLO)] = HELLO
    img[8 * BLOCK:8 * BLOCK + len(NOTE)] = NOTE

    # --- DOCS dir (block 7) ---
    o = 7 * BLOCK
    at = 0
    for ino, nm, ft in ((3, b".", 2), (2, b"..", 2)):
        e = dirent(ino, nm, ft)
        img[o + at:o + at + len(e)] = e
        at += len(e)
    e = dirent(5, b"NOTE.TXT", 1)
    img[o + at:o + at + len(e)] = e
    struct.pack_into("<H", img, o + at + 4, BLOCK - at)

    return bytes(img)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "images/ext4.raw"
    img = build()
    with open(out, "wb") as f:
        f.write(img)
    print(f"[ok] ext4 image {len(img)}B -> {out}")


if __name__ == "__main__":
    main()
