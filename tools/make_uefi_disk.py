#!/usr/bin/env python3
"""Build a disposable MBR/FAT32 UEFI disk image for VirtualBox.
Writes only to build/uefi; never touches physical disks. Stdlib only.
"""
from pathlib import Path
import math
import struct
import sys

ROOT = Path(__file__).resolve().parents[1] if Path(__file__).parent.name == 'tools' else Path.cwd()
EFI = ROOT / 'build' / 'uefi' / 'esp' / 'EFI' / 'BOOT' / 'BOOTX64.EFI'
KERNEL = ROOT / 'build' / 'uefi' / 'nova-kernel-x64.elf'
OUT = ROOT / 'build' / 'uefi' / 'nova-uefi-fat32.img'
SECTOR = 512
DISK_SECTORS = 262144  # 128 MiB
PART_START = 2048
PART_SECTORS = DISK_SECTORS - PART_START
RESERVED = 32
FATS = 2
SECTORS_PER_CLUSTER = 1

def pack_at(buf, offset, fmt, *values):
    struct.pack_into(fmt, buf, offset, *values)

def entry(name, attr, start_cluster, size=0):
    assert len(name) == 11
    b = bytearray(32)
    b[0:11] = name
    b[11] = attr
    pack_at(b, 20, '<H', start_cluster >> 16)
    pack_at(b, 26, '<H', start_cluster & 0xffff)
    pack_at(b, 28, '<I', size)
    return b

def main():
    payload = EFI.read_bytes()
    kernel = KERNEL.read_bytes()
    if not kernel.startswith(b'\x7fELF') or len(kernel) > 1024*1024:
        raise ValueError('kernel ELF missing or invalid; run python tools/uefi.py build')
    if not payload.startswith(b'MZ') or not payload or len(payload) > 16*1024*1024:
        raise ValueError('BOOTX64.EFI missing, invalid or too large; run python tools/uefi.py build')
    fat_sectors = 1
    while True:
        clusters = (PART_SECTORS - RESERVED - FATS * fat_sectors)//SECTORS_PER_CLUSTER
        needed = math.ceil((clusters + 2)*4/SECTOR)
        if needed == fat_sectors:
            break
        fat_sectors = needed
    if not (65525 <= clusters < 0x0ffffff5):
        raise ValueError('FAT32 cluster count unsupported')
    file_clusters = math.ceil(len(payload)/SECTOR)
    kernel_clusters = math.ceil(len(kernel)/SECTOR)
    if 5 + file_clusters + kernel_clusters >= clusters:
        raise ValueError('EFI too large for partition')
    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open('wb') as f:
        f.truncate(DISK_SECTORS * SECTOR)
        mbr = bytearray(SECTOR)
        # Standard protective-free MBR with EF EFI System Partition.
        pe = 446
        mbr[pe+4] = 0xEF
        pack_at(mbr, pe+8, '<I', PART_START)
        pack_at(mbr, pe+12, '<I', PART_SECTORS)
        mbr[510:512] = b'\x55\xaa'
        f.seek(0); f.write(mbr)

        boot = bytearray(SECTOR)
        boot[0:3] = b'\xeb\x58\x90'
        boot[3:11] = b'MSDOS5.0'
        pack_at(boot, 11, '<H', SECTOR)
        boot[13] = SECTORS_PER_CLUSTER
        pack_at(boot, 14, '<H', RESERVED)
        boot[16] = FATS
        pack_at(boot, 17, '<H', 0)
        pack_at(boot, 19, '<H', 0)
        boot[21] = 0xF8
        pack_at(boot, 22, '<H', 0)
        pack_at(boot, 24, '<H', 63)
        pack_at(boot, 26, '<H', 255)
        pack_at(boot, 28, '<I', PART_START)
        pack_at(boot, 32, '<I', PART_SECTORS)
        pack_at(boot, 36, '<I', fat_sectors)
        pack_at(boot, 40, '<H', 0)
        pack_at(boot, 42, '<H', 0)
        pack_at(boot, 44, '<I', 2)
        pack_at(boot, 48, '<H', 1)
        pack_at(boot, 50, '<H', 6)
        boot[64] = 0x80
        boot[66] = 0x29
        pack_at(boot, 67, '<I', 0x4e4f5641)
        boot[71:82] = b'NOVAUEFI   '
        boot[82:90] = b'FAT32   '
        boot[510:512] = b'\x55\xaa'
        f.seek(PART_START * SECTOR); f.write(boot)
        f.seek((PART_START + 6)*SECTOR); f.write(boot)

        fsinfo = bytearray(SECTOR)
        pack_at(fsinfo, 0, '<I', 0x41615252)
        pack_at(fsinfo, 484, '<I', 0x61417272)
        pack_at(fsinfo, 488, '<I', clusters - 3 - file_clusters - kernel_clusters)
        pack_at(fsinfo, 492, '<I', 5 + file_clusters + kernel_clusters)
        pack_at(fsinfo, 508, '<I', 0xAA550000)
        for sector in (1, 7):
            f.seek((PART_START + sector)*SECTOR); f.write(fsinfo)

        fat = bytearray(fat_sectors * SECTOR)
        def mark(c, value): pack_at(fat, c*4, '<I', value)
        mark(0, 0x0ffffff8); mark(1, 0x0fffffff)
        for c in (2, 3, 4): mark(c, 0x0fffffff)
        for i in range(file_clusters):
            mark(5+i, 5+i+1 if i != file_clusters-1 else 0x0fffffff)
        kernel_start = 5 + file_clusters
        for i in range(kernel_clusters):
            mark(kernel_start+i, kernel_start+i+1 if i != kernel_clusters-1 else 0x0fffffff)
        for i in range(FATS):
            f.seek((PART_START + RESERVED + i*fat_sectors)*SECTOR)
            f.write(fat)
        data_lba = PART_START + RESERVED + FATS*fat_sectors
        def cluster(c, value):
            f.seek((data_lba + c-2)*SECTOR)
            f.write(bytes(value).ljust(SECTOR, b'\0'))
        cluster(2, entry(b'EFI        ', 0x10, 3))
        cluster(3, entry(b'.          ', 0x10, 3) + entry(b'..         ', 0x10, 2) + entry(b'BOOT       ', 0x10, 4))
        cluster(4, entry(b'.          ', 0x10, 4) + entry(b'..         ', 0x10, 3) + entry(b'BOOTX64 EFI', 0x20, 5, len(payload)) + entry(b'NOVAKERNELF', 0x20, kernel_start, len(kernel)))
        for i in range(file_clusters):
            cluster(5+i, payload[i*SECTOR:(i+1)*SECTOR])
        for i in range(kernel_clusters):
            cluster(kernel_start+i, kernel[i*SECTOR:(i+1)*SECTOR])
    print(f'FAT32-UEFI-IMAGE-OK {OUT} ({OUT.stat().st_size} bytes, {file_clusters} EFI clusters, {kernel_clusters} ELF clusters)')

if __name__ == '__main__':
    try: main()
    except (OSError, ValueError) as exc:
        sys.exit(f'[FAIL] {exc}')
