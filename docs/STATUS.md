# Project Status

**Milestone:** M2 — Controlled execution  
**State:** public clean-room baseline green; Linux native transition/fault recovery in review  
**Repository:** astraea-emu/astraea  
**Active branch:** `feat/m2-linux-transition-recovery`

## Complete

- M0 engineering foundation.
- M1 validated `GuestImage`.
- M2 guarded native x86-64 execution architecture.
- M2 portable `GuestCpuContext`, backend stop/fault model, and execution-memory planner.
- Public clean-room baseline migrated without prior Git history.
- Linux exact-address guest memory preparation and teardown (#2).
- Synthetic host-gate / HLE dispatch ABI specification (#3).
- Synthetic HLE registry and gate-region model (#13).
- Public five-gate CI:
  - Linux x64
  - Windows x64
  - macOS ARM64
  - Linux ASan + UBSan
  - Linux Clang fuzz smoke

## Current frontier

1. #4 — Linux native register transition and scoped fault recovery — in review on this branch.
2. #14 — bounded guest-memory access plus `astraea.test.write` and `astraea.test.exit`.
3. #15 — first end-to-end `probe_hello.elf`.
4. #5 — equivalent guarded Windows x86-64 backend.

## #4 scope

- dedicated Linux execution thread
- exact RX mapping of backend-generated synthetic gate bytes
- guest GPR/RIP/RSP installation through Linux `ucontext`
- alternate signal stack
- scoped SIGSEGV/SIGBUS/SIGILL/SIGFPE handling
- exact guest/gate RIP ownership checks
- recognized gate-slot capture
- normalized guest fault capture
- `sigsetjmp` / `siglongjmp` recovery to a known host stack
- previous signal dispositions restored after every stop/failure

No HLE handler executes in signal context.

## Execution boundary

Native execution v0 remains limited to trusted Astraea-owned synthetic probes.
Arbitrary retail guest execution is not enabled.

## Clean-room boundary

Do not commit Sony firmware, Sony keys, proprietary SDK material, decrypted
retail game assets, copyrighted PS5 executables, proprietary system modules,
DRM-bypass material, or unrelated employer/proprietary material.

PS5-specific assumptions require documented evidence.

## Next action

Validate #4 across the public five-gate matrix. After merge, implement #14 and
then reach #15 `probe_hello.elf`.
