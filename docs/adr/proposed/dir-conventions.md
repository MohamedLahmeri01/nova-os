# Proposed ADR — Architecture/platform directory conventions (number assigned on ratification)

Status: PROPOSED — NOT RATIFIED. Date: 2026-10-09.

## Context

`kernel/arch/` holds two header-only backends;
`boot/` mixes protocol and board knowledge; no
`plat/` or `drv/` exist. Options: (a) adopt
`kernel/{arch/<isa>,plat/<board>,drv/<family>}`
+ `plat/<board>/manifest` with a lint-enforced
map (which dirs may include what); (b) keep
flat + document (reject: unenforceable, rots);
(c) mirror Linux drivers//arch// verbatim
(reject: scale-inappropriate, Kconfig culture).

## Trade-offs

(a) is small, tailored, and checkable by the
existing lint driver. Cost: moving files breaks
blame continuity once (one commit, well
described) — worth it before M2 multiplies
callers. No code semantics change in the move.

## Prerequisites / evidence to decide

Portability-boundaries proposal ratified first
(conventions encode contracts, not precede
them); move scripted + reviewed, gates green
before/after with identical serial bytes.
