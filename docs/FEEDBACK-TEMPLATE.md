# NOVA OS v0.8.0 — Tester Feedback Template

Copy, fill in, and submit through the repository
issue tracker (channel chosen by the project
owner). Redact credentials, tokens, personal
data, and unrelated host details first.

```text
NOVA version / artifact: v0.8.0 / <filename>
Artifact SHA-256 (your computed value):
Host OS:            Example: Windows 11 23H2
VirtualBox version: Example: 7.0.18
Guest CPUs / RAM:   Example: 2 / 1024 MB
Storage layout:     Example: IDE 4×1 MB as documented
Checksum result:    PASS / FAIL (stop here if FAIL)
First boot:         PASS / FAIL
Quickstart followable unaided: YES / NO (step?):
Tests attempted (mark each PASS/FAIL/BLOCKED):
  [ ] help / about / uptime / stat
  [ ] put + cat on /disk
  [ ] poweroff + cold boot + cat again
  [ ] rm + read-after-remove
  [ ] /nova round trip
  [ ] bogus command + bad path errors
  [ ] exit guidance
  [ ] ping (gateway reply) + off-subnet refusal
  [ ] nslookup example.com (public answer)
Failure (if any):
  Expected:
  Actual:
  Exact steps:
  Frequency: once / sometimes / always
  Severity: cosmetic / confusing / data-loss / boot-failure
  Fresh-VM reproduce: YES / NO
Evidence attached: screenshot / serial excerpt / none
Suggestions:
```

## Questionnaire (answer in plain words)

1. Could you start NOVA without help?
2. Which step was confusing or failed?
3. Which documented feature did you try?
4. Did persistence behave as documented?
5. What was the most serious bug?
6. What should be improved next?
7. Would you test another build?
