# Contributing

Phase 0. Small, reviewed, tested changes only.

## Workflow

1. Read `docs/ARCHITECTURE.md` + relevant ADR before touching code.
2. Smallest coherent change; follow the agent loop:
   understand → inspect → design → implement → compile → test →
   benchmark (if perf) → security review → document → report.
3. Commit messages: `subsystem: imperative summary` (e.g.
   `mm: implement physical page allocator`). No `update/fix/stuff`.
4. Do not claim done without evidence (see master spec §42).

## Gates

Format, lint, compile, unit tests, and (from Phase 1) VBox boot test.
AI-generated code follows the same gates — no bypass.

## Good first issues

Docs, tests, build stub improvements, VBox test automation — anything
that doesn't require whole-kernel understanding.
