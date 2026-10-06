# tests/integration/ — intent (not yet staffed)

This directory is reserved for integration tests that need more
than one unit: multi-syscall guest scenarios, cross-subsystem
flows (FS → exec → sockets), and VM-boot assertions beyond the
gate chain.

Today those scenarios live inside guest selftests
(`SOCKBLK-OK`, `STARVE-OK`, …) and `tools/nova.py test`
(T5/T6 boots). When the first true multi-unit host harness
lands, it goes here — not scattered across `tests/unit/`.

Convention (from TESTING-STRATEGY): hermetic, deterministic,
fail-loud with the exact `nova.py` repro command.
