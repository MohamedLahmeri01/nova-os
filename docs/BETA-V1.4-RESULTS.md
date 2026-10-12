# NOVA OS v0.8.0 — Pilot Results (Beta V1.4)

Candidate: `dist/NOVA-OS-Beta-v0.8.0.zip`
SHA-256:
`b6c24bc6b566ffc8fa3ff5cd81c9e40f46d4609a219f36f82bdafaa6a01208cb`

## Test matrix (per tester; outcomes: PASS / FAIL /
## BLOCKED / NOT RUN / INCONCLUSIVE)

| # | Test | Tester A | Tester B | Tester C |
| Artifact checksum verification | | NOT RUN | NOT RUN | NOT RUN |
| Archive extraction | | NOT RUN | NOT RUN | NOT RUN |
| Fresh disposable VM setup | | NOT RUN | NOT RUN | NOT RUN |
| First boot | | NOT RUN | NOT RUN | NOT RUN |
| Shell availability | | NOT RUN | NOT RUN | NOT RUN |
| help / about / uptime | | NOT RUN | NOT RUN | NOT RUN |
| Persistent file create+read | | NOT RUN | NOT RUN | NOT RUN |
| Clean shutdown + cold reboot | | NOT RUN | NOT RUN | NOT RUN |
| Byte-exact persistence verify | | NOT RUN | NOT RUN | NOT RUN |
| Test-file removal | | NOT RUN | NOT RUN | NOT RUN |
| Failed-setup recovery | | NOT RUN | NOT RUN | NOT RUN |
| Instructions usable unaided | | NOT RUN | NOT RUN | NOT RUN |

## Locally verified reference (developer runs, same bits)

- Clean-VM boot + first run + persistence +
  removal: PASS (prior campaign, screenshots).
- Suite 23/23 ×3, gate, panic: PASS.
- Package checksums: ALL MATCH (this phase).

## Status

NO EXTERNAL REPORTS RECEIVED. Zero testers have
returned evidence. Per the rules, every cell
above is NOT RUN — nothing is claimed, nothing
is PASS by default. The pilot is PREPARED but
NOT COMPLETED. Next action: owner invites 3–5
testers and fills this matrix from real reports.
