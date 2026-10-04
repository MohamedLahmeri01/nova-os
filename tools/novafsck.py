#!/usr/bin/env python3
"""NOVA OS filesystem checker, host side (Phase 7f-3).

Verify-only audit of a NOVA-FS v0.1 image (see docs/NOVA-FS.md):
superblock copies, journal coherence, bitmaps-vs-reality, inode
checksums, directory chains, orphan detection. Prints one line per
issue; exit 0 when clean, 1 with issues, 2 on I/O failure. No
repairs in v0.1 (verify first, fix later).

Usage: novafsck.py <image> [--quiet]
"""
import struct
import sys

BLOCK = 4096
INO_SIZE = 128
EXT_MAGIC = 0xF30A
J_MAGIC = 0x4A524E4C
C_MAGIC = 0x434F4D54


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


class Img:
    def __init__(self, data):
        self.d = data
        self.nblocks = len(data) // BLOCK

    def block(self, b):
        if b >= self.nblocks:
            return None
        return self.d[b * BLOCK:(b + 1) * BLOCK]


def check_sb(img, issues):
    """Returns layout dict or None (both copies bad)."""
    copies = []
    for blk in (0, img.nblocks - 1):
        b = img.block(blk)
        if b is None:
            issues.append(f"sb: block {blk} unreadable")
            return None
        sb = b[1024:2048]
        if sb[0:8] != b"NOVAFS01":
            issues.append(f"sb@{blk}: bad magic {sb[0:8]!r}")
            continue
        if struct.unpack_from("<I", sb, 8)[0] != BLOCK:
            issues.append(f"sb@{blk}: bad block size")
            continue
        if crc32c(bytes(sb[0:48])) != struct.unpack_from("<I", sb, 48)[0]:
            issues.append(f"sb@{blk}: checksum mismatch")
            continue
        if struct.unpack_from("<I", sb, 44)[0] != 0:
            issues.append(f"sb@{blk}: unknown features")
            continue
        copies.append((blk, sb))
    if not copies:
        issues.append("sb: no valid copy")
        return None
    if len(copies) == 1:
        issues.append(f"sb: only copy at block {copies[0][0]} verifies")
    _, sb = copies[0]
    total = struct.unpack_from("<I", sb, 12)[0]
    if total != img.nblocks:
        issues.append(f"sb: total {total} != image blocks {img.nblocks}")
    return {
        "total": total,
        "free_blocks": struct.unpack_from("<I", sb, 16)[0],
        "free_inodes": struct.unpack_from("<I", sb, 24)[0],
        "root": struct.unpack_from("<I", sb, 28)[0],
        "jstart": struct.unpack_from("<I", sb, 32)[0],
        "jcount": struct.unpack_from("<I", sb, 36)[0],
        "inotab": struct.unpack_from("<I", sb, 96)[0],
        "bbmap": struct.unpack_from("<I", sb, 100)[0],
        "ibmap": struct.unpack_from("<I", sb, 104)[0],
    }


def check_journal(img, sb, issues):
    """Descriptors need matching valid commits; torn tails (no valid
    commit) are legal recovery states, reported, not errors."""
    if sb is None:
        return
    for b in range(sb["jstart"], sb["jstart"] + sb["jcount"]):
        blk = img.block(b)
        if blk is None:
            issues.append(f"journal: block {b} unreadable")
            return
        if blk[0:4] != struct.pack("<I", J_MAGIC):
            continue
        magic, seq, count, crc = struct.unpack_from("<IIII", blk, 0)
        if magic != J_MAGIC or count == 0 or count > 8:
            continue
        calc = crc32c(bytes(blk[0:12]) + bytes(blk[16:16 + 8 * count]))
        if calc != crc:
            issues.append(f"journal@{b}: descriptor crc mismatch")
            continue
        cb = img.block(b + 1 + count)
        if cb is None:
            issues.append(f"journal@{b}: commit block out of range")
            continue
        cm, cs, cc = struct.unpack_from("<III", cb, 0)
        if cm != C_MAGIC or cs != seq or \
                crc32c(bytes(cb[0:8])) != cc:
            issues.append(f"journal@{b}: torn tail (no valid commit)")


def read_inode(img, sb, ino):
    """Returns 128B or None."""
    idx = ino - 1
    tb = img.block(sb["inotab"] + idx // 32)
    if tb is None:
        return None
    return tb[(idx % 32) * 128:(idx % 32) * 128 + 128]


def walk_inodes(img, sb, issues):
    """Returns (ref_blocks set, links dict ino->count)."""
    ref_blocks = set()
    links = {}
    if sb is None:
        return ref_blocks, links
    ibitmap = img.block(sb["ibmap"])
    if ibitmap is None:
        issues.append("bitmap: inode bitmap unreadable")
        return ref_blocks, links
    for ino in range(1, 17):
        used = bool(ibitmap[(ino - 1) // 8] & (1 << ((ino - 1) % 8)))
        ib = read_inode(img, sb, ino)
        if ib is None:
            issues.append(f"inode: {ino} unreadable")
            return ref_blocks, links
        if not used:
            if any(ib):
                issues.append(f"inode {ino}: free bit but nonzero bytes")
            continue
        if ino == 1:
            # reserved boot inode: bit set, bytes must be zero
            if any(ib):
                issues.append("inode 1: reserved but nonzero")
            continue
        if crc32c(bytes(ib[0:112])) != struct.unpack_from("<I", ib, 112)[0]:
            issues.append(f"inode {ino}: checksum mismatch")
            continue
        links[ino] = struct.unpack_from("<H", ib, 26)[0]
        if struct.unpack_from("<H", ib, 40)[0] != EXT_MAGIC:
            issues.append(f"inode {ino}: bad extent magic")
            continue
        if struct.unpack_from("<H", ib, 44)[0] != 0:
            issues.append(f"inode {ino}: nonzero depth (unsupported)")
            continue
        count = struct.unpack_from("<H", ib, 42)[0]
        if count > 4:
            issues.append(f"inode {ino}: too many extents")
            continue
        for e in range(count):
            s = struct.unpack_from("<I", ib, 48 + e * 8)[0]
            ln = struct.unpack_from("<H", ib, 48 + e * 8 + 4)[0]
            for k in range(ln):
                if s + k >= sb["total"]:
                    issues.append(f"inode {ino}: extent past end")
                ref_blocks.add(s + k)
    return ref_blocks, links


def walk_dirs(img, sb, issues):
    """Returns (seen_dirs, seen_files). Validates chains + targets."""
    seen_dirs = set()
    seen_files = set()
    if sb is None:
        return seen_dirs, seen_files
    stack = [sb["root"]]
    while stack:
        ino = stack.pop()
        if ino in seen_dirs:
            continue
        seen_dirs.add(ino)
        ib = read_inode(img, sb, ino)
        if ib is None:
            issues.append(f"dir {ino}: unreadable")
            continue
        if struct.unpack_from("<H", ib, 0)[0] & 0xF000 != 0x4000:
            issues.append(f"dir walk: inode {ino} not a dir")
            continue
        # v0.1: single direct block (extent 0)
        if struct.unpack_from("<H", ib, 42)[0] == 0:
            continue
        dblk = struct.unpack_from("<I", ib, 48)[0]
        db = img.block(dblk)
        if db is None:
            issues.append(f"dir {ino}: block {dblk} unreadable")
            continue
        at = 0
        while at + 8 <= BLOCK:
            ei, rl, nl, ft = struct.unpack_from("<IHBB", db, at)
            if rl < 8 or at + rl > BLOCK or rl == 0:
                issues.append(f"dir {ino}: bad rec_len at {at}")
                break
            if ei != 0:
                if nl == 0 or nl > 255:
                    issues.append(f"dir {ino}: bad name len at {at}")
                elif nl <= 2 and db[at + 8:at + 8 + nl] in (b".", b".."):
                    pass
                else:
                    cib = read_inode(img, sb, ei)
                    if cib is None:
                        issues.append(f"dir {ino}: target {ei} unreadable")
                    elif ft == 2:
                        stack.append(ei)
                    else:
                        seen_files.add(ei)
            at += rl
    return seen_dirs, seen_files


def main():
    quiet = "--quiet" in sys.argv
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if not args:
        print("usage: novafsck.py <image> [--quiet]", file=sys.stderr)
        return 2
    try:
        with open(args[0], "rb") as f:
            data = f.read()
    except OSError as e:
        print(f"fsck: cannot read: {e}", file=sys.stderr)
        return 2
    img = Img(data)
    issues = []
    sb = check_sb(img, issues)
    check_journal(img, sb, issues)
    ref_blocks, links = walk_inodes(img, sb, issues)
    seen_dirs, seen_files = walk_dirs(img, sb, issues)
    if sb is not None:
        bb = img.block(sb["bbmap"])
        ib = img.block(sb["ibmap"])
        if bb is None:
            issues.append("bitmap: block bitmap unreadable")
        else:
            marked = {b for b in range(sb["total"])
                      if bb[b // 8] & (1 << (b % 8))}
            for b in sorted(ref_blocks - marked):
                issues.append(f"bitmap: block {b} referenced but free")
            meta = set(range(0, 72)) | {sb["total"] - 1}
            for b in sorted(marked - ref_blocks - meta):
                issues.append(f"bitmap: block {b} marked but unrefd")
            used = marked | ref_blocks
            if sb["free_blocks"] != sb["total"] - len(used):
                issues.append("sb: free_blocks mismatch")
        if ib is None:
            issues.append("bitmap: inode bitmap unreadable")
        else:
            marked_i = {i for i in range(1, 17)
                        if ib[(i - 1) // 8] & (1 << ((i - 1) % 8))}
            for i in sorted(set(links) - marked_i):
                issues.append(f"bitmap: inode {i} used but bit free")
            for i in sorted(marked_i - set(links) - {1}):
                issues.append(f"bitmap: inode {i} bit set but unused")
            if sb["free_inodes"] != 16 - len(marked_i):
                issues.append("sb: free_inodes mismatch")
        for i in sorted(set(links) - seen_dirs - seen_files):
            issues.append(f"orphan: inode {i} allocated, unreachable")
    if issues and not quiet:
        for line in issues:
            print(line)
    if not quiet:
        print(f"fsck: {len(issues)} issue(s)")
    return 1 if issues else 0


if __name__ == "__main__":
    sys.exit(main())
