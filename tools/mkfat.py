#!/usr/bin/env python3
"""NOVA OS FAT32 data-disk generator (Phase 7c).

Builds a deterministic superfloppy FAT32 image (no MBR, BPB at LBA0):
reserved=32, 2 FATs of 1 sector, 1 sector/cluster, root at cluster 2.

Contents (CONTRACT: kernel fat_selftest asserts these exact bytes):
  /HELLO.TXT      "hello from fat32\\n"  (17B, cluster 3)
  /DOCS/NOTE.TXT  "fat32 nested ok\\n"   (17B, cluster 5)

Fixed timestamps, fixed volume ID, zero padding: rebuilding yields
byte-identical output. The VDI step pads to 1MB (zeros past the
BPB total-sector count, which the driver must respect).
"""
import struct
import sys

SECTOR = 512
TOTAL_SECTORS = 64
RESERVED = 32
FAT_SECTORS = 1
DATA_START = RESERVED + 2 * FAT_SECTORS  # LBA 34
HELLO = b"hello from fat32\n"
NOTE = b"fat32 nested ok\n"
VOL_ID = 0x4E4F5641
FAT_DATE = 0x5D43  # 2026-10-03, fixed


def dirent(name11, attr, cluster, size):
    e = bytearray(32)
    e[0:11] = name11
    e[11] = attr
    struct.pack_into("<H", e, 20, (cluster >> 16) & 0xFFFF)
    struct.pack_into("<H", e, 26, cluster & 0xFFFF)
    struct.pack_into("<I", e, 28, size)
    struct.pack_into("<H", e, 16, 0)  # time: 00:00:00
    struct.pack_into("<H", e, 18, FAT_DATE)
    return bytes(e)


def build():
    img = bytearray(TOTAL_SECTORS * SECTOR)
    # NOTE: bytearray slices COPY; all writes below use absolute
    # offsets into img (pack_into / slice-assign on img itself).

    # --- boot sector / BPB (LBA 0) ---
    o = 0
    img[o:o + 3] = bytes([0xEB, 0x58, 0x90])
    img[o + 3:o + 11] = b"NOVAOS  "
    struct.pack_into("<H", img, o + 11, SECTOR)
    img[o + 13] = 1  # sectors per cluster
    struct.pack_into("<H", img, o + 14, RESERVED)
    img[o + 16] = 2  # FATs
    img[o + 21] = 0xF8  # media
    # NB: FAT32 total-sectors-32 lives at offset 32 (28 is hidden).
    struct.pack_into("<I", img, o + 32, TOTAL_SECTORS)
    struct.pack_into("<I", img, o + 36, FAT_SECTORS)
    struct.pack_into("<H", img, o + 44, 2)  # root cluster
    struct.pack_into("<H", img, o + 48, 1)  # FSInfo sector
    struct.pack_into("<H", img, o + 50, 6)  # backup boot sector
    img[o + 64] = 0x80
    img[o + 66] = 0x29
    struct.pack_into("<I", img, o + 67, VOL_ID)
    img[o + 71:o + 82] = b"NOVADATA   "
    img[o + 82:o + 90] = b"FAT32   "
    img[o + 510:o + 512] = bytes([0x55, 0xAA])

    # --- FSInfo (LBA 1) + backup boot (LBA 6) ---
    o = SECTOR
    struct.pack_into("<I", img, o + 0, 0x41615252)
    struct.pack_into("<I", img, o + 484, 0x61417272)
    struct.pack_into("<I", img, o + 488, TOTAL_SECTORS - DATA_START - 4)
    struct.pack_into("<I", img, o + 492, 2)
    img[o + 510:o + 512] = bytes([0x55, 0xAA])
    img[6 * SECTOR:7 * SECTOR] = img[0:SECTOR]

    # --- FATs (LBA 32, 33) ---
    for base in (RESERVED, RESERVED + FAT_SECTORS):
        o = base * SECTOR
        for i, v in ((0, 0x0FFFFFF8), (1, 0x0FFFFFFF), (2, 0x0FFFFFFF),
                     (3, 0x0FFFFFFF), (4, 0x0FFFFFFF), (5, 0x0FFFFFFF)):
            struct.pack_into("<I", img, o + 4 * i, v)

    def clus(n):
        return DATA_START + (n - 2)

    # --- root dir (cluster 2) ---
    o = clus(2) * SECTOR
    img[o:o + 32] = dirent(b"HELLO   TXT", 0x20, 3, len(HELLO))
    img[o + 32:o + 64] = dirent(b"DOCS       ", 0x10, 4, 0)

    # --- file data ---
    img[clus(3) * SECTOR:clus(3) * SECTOR + len(HELLO)] = HELLO
    img[clus(5) * SECTOR:clus(5) * SECTOR + len(NOTE)] = NOTE

    # --- DOCS dir (cluster 4): dot, dotdot, NOTE ---
    o = clus(4) * SECTOR
    img[o:o + 32] = dirent(b".          ", 0x10, 4, 0)
    img[o + 32:o + 64] = dirent(b"..         ", 0x10, 2, 0)
    img[o + 64:o + 96] = dirent(b"NOTE    TXT", 0x20, 5, len(NOTE))

    return bytes(img)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "images/fat32.raw"
    img = build()
    with open(out, "wb") as f:
        f.write(img)
    print(f"[ok] fat32 image {len(img)}B -> {out}")


if __name__ == "__main__":
    main()
