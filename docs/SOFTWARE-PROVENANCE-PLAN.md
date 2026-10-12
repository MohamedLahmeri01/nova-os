# NOVA OS — Software Provenance Plan (Phase M: M0)

PROPOSED plan (no SBOM tooling installed here).
An SBOM is a verified inventory, not a file format
wishlist: no claim of completeness until the
methodology below runs green twice.

## 1. Inventory categories (every artifact lands in one)

- First-party: kernel/, userspace/, boot/, sdk/,
  tools/, tests/, ci/ (git SHAs = versions).
- Imported (source in-tree, upstream-owned):
  TODAY: none known — verify by license-header
  sweep (any non-Apache header is a finding).
  FUTURE: TCP stack, crypto lib, FS, fonts
  (gui/font.c — CHECK license header in M0).
- Vendored (binary/blob): TODAY: none known.
- Generated: *.bin/*.raw/*.vdi (gitignored —
  see .gitignore), corpora outputs. Never
  versioned, always regenerable, hashed per §3
  of the build plan.
- Build tools: mingw-w64 gcc + ld + Python +
  stdlib-only driver (record versions per run).
- Runtime deps of the IMAGE: none (freestanding;
  state it every release — the day this changes,
  this plan changes first).

## 2. Per-entry fields

Name, category, version/commit, license (+
license text location), source URL, NOVA-side
owner, consuming components, update cadence,
advisory feed (or "none — gap" with owner +
date), and removal procedure (how to excise it).

## 3. Pinning and advisories

- Pins: exact versions/commits in one file
  (to be created; absent today). `latest` is
  never a pin.
- Advisory watch: every imported/vendored entry
  gets a feed or a named human watcher + check
  interval. Unwatched entries are findings.
- Security fix flow: advisory → triage (per
  SECURITY.md once it exists) → patch/backport →
  regression run → advisory note in CHANGELOG.
- Removal: excise + gate-green + note; dead
  entries leave no build references (grep gate).

## 4. The gui/font.c question (M0 action)

The BIOS-font-derived glyph data predates the
audit trail. M0 must establish: source, license
compatibility with Apache-2.0 distribution, and
a replacement plan if incompatible. This is the
only known first-party provenance question; the
license-header sweep confirms whether it is the
only one.

## 5. Completeness bar (M0 exit)

Inventory generated twice, independently, same
result; every entry has all §2 fields; sweep
finds no unlisted third-party bytes; fonts
resolved. Until then: "SBOM in progress," never
"SBOM complete."
