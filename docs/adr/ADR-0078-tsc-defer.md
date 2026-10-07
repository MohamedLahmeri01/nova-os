# ADR-0078: TSC deferred — PIT stays authoritative (v0.5 Phase E)

Status: accepted (v0.5). Date: 2026-10-07.

## Context

Phase E audited every time consumer (`docs/TIMEBASE.md`):
all tick-native, largest horizon 8000 ticks, all
comparisons wrap-safe. The only open question was whether
the Trustworthy-looking TSC should replace or augment the
PIT. Measured (temporary guest diagnostic, since removed):
AuthenticAMD Zen 3, TSC + invariant TSC present, no
frequency leaves, ~3.6 GHz vs PIT (matches host base
clock), `rdtsc` costs ~13,464 cycles (hypervisor trap).

## Decision

DEFER. PIT remains the authoritative timebase. TSC is
recorded available for future monotonic work but not
adopted: frequency undiscoverable in-guest, reads mediated
and expensive, zero consumers need sub-tick precision.

## Consequences

- No TSC code in the tree (diagnostic removed pre-commit).
- Benchmarks stay tick-granular; 0 ticks reads as <10 ms.
- No high-res user clock (side-channel surface unchanged).
- Revisit needs: real sub-tick consumer + boot
  calibration + SMP sync story. That is a new ADR, not
  a continuation of this one.
