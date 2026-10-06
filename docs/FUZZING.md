# NOVA Fuzzing Contract (v0.4 Phase K, normative for new parsers)

Every parser below states its contract. Violations panic the
suite (never the kernel): forbidden behaviors are attached to
regression seeds, not incident reports.

## Per-parser contracts

```text
parser              input                    behavior                          max runtime   errors allowed            FORBIDDEN
dhcp_parse_msg      300B msgs (corpus)       validate XID/chaddr/magic/      trivial       EINVAL only               hang, crash, accept-rogue
                                             options; extract lease fields
dns_parse/build     ≤512B packets (corpus)   grammar + bounds + jump caps;   trivial       EINVAL/ENOENT             hang, crash, CNAME-accept
                                             first-A-wins, direct only
net_ip_parse        ≤1500B datagrams         version/IHL/len/cksum/TTL/frag  trivial       EINVAL                    hang, crash, frag-accept
net_udp_parse       ≤1472B segments          len + checksum (0 = absent)     trivial       EINVAL                    hang, crash, trunc-accept
fat32 mount/walk    32KB image + mutants     BPB/markers/geometry; step caps; bounded       EIO/EINVAL/ENOENT/        hang (chain loops!),
                                             spins; grow-only writes                       ENOSPC/EISDIR/EROFS       OOB read, silent corrupt
ext4 mount/walk     128KB image + mutants    enforced geometry; rec_len      bounded       EIO/EINVAL/ENOENT         hang (reclen!), OOB,
                                             progress; direct+indirect only                                          silent corrupt
nova mount/walk     512KB image + mutants    SB copies+crc; journal replay;  bounded       EIO/EINVAL/ENOENT/        hang, OOB, torn-accept,
                                             per-inode crc; bitmap(fsck)     (fsck: full scan) ENOSPC                silent corrupt
shell dispatch      ≤300B lines (corpus +    prefix match; bounded arg       trivial       usage strings,          hang (all loops advance),
                    3k PRNG soup)            parsers; strtoi saturates       EIO-stubbed   -EIO paths              OOB write, UB overflow
syscall validate    u32 ptr+len (20k PRNG    range+wrap+cap+per-page walk    trivial       EINVAL/EFAULT             hang, crash, wrong-errno
                    + edge corpus)           vs oracle
sock discipline      2k op fuzz              errno matrix                    trivial       EBADF/EAGAIN/             hang (frozen ticks!),
                                                                           ENOSPC/EBUSY/                         wrong-errno
                                                                           EADDRINUSE/EMFILE
```

Runtime notes: "trivial" = microseconds, no allocation beyond
bounded statics/stacks; "bounded" = step-capped walks + fixed
caps (FS_MAX_FILE 64KB, NET_MTU 1500, RXQ 4, sock pool 8).
Frozen-tick hosts must use timeout-0 probes only (a nonzero
rx_wait timeout spins forever without ticks — T21 learned
this; contract pins it).

## Corpus map (tests/corpus/, replayed every suite run)

- `dhcp/`, `dns/` — wire seeds (T14, argv dir).
- `udp/` — wire shapes (T21, argv dir).
- `fat32/`, `ext4/`, `nova_fs/` — `MUTANTS.txt`
  (offset/hex/expect; T10/T11/T12 driver rows + T13 fsck
  rows; fixtures regenerated deterministically first).
- `shell/` — one line per file (T23, argv dir).
- Generators: `tools/mkcorpus.py` (wire+shell bytes),
  `tools/mk{fat,ext4,nova}.py` (fixtures). Manifests are
  hand-designed (offsets from the generators), never
  recorded blind: predict → run → investigate mismatch.

## Bug → seed rule (non-negotiable)

Every parser bug found anywhere (review, suite, manual)
lands as: (1) a corpus file or manifest row, (2) its
expectation in the replay table, (3) the fix, (4) an ADR
note if architectural. Inline-only regression vectors
are tech debt from birth. Precedents set this phase:
strtoi overflow (T23 vectors), self-loop dir walk
(`dir-loop` manifest row), HELLO-inode offset discipline
(wrong-offset lesson in ADR-0073).
