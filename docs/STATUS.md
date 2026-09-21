# Project Status

**Milestone:** M2 — Controlled execution  
**State:** public clean-room baseline green; Linux memory preparation complete; synthetic HLE gate contract accepted  
**Repository:** astraea-emu/astraea  
**Active branch:** `main`

## Complete

- M0 engineering foundation.
- M1 validated `GuestImage`.
- M2 guarded native x86-64 execution architecture.
- M2 portable `GuestCpuContext`, backend stop/fault model, and execution-memory planner.
- Public clean-room baseline migrated without prior Git history.
- Linux exact-address guest memory preparation and teardown (#2).
- Synthetic host-gate / HLE dispatch ABI specification (#3).
- Public five-gate CI:
  - Linux x64
  - Windows x64
  - macOS ARM64
  - Linux ASan + UBSan
  - Linux Clang fuzz smoke

## Current frontier

1. #13 — implement the synthetic HLE registry and backend-owned gate region.
2. #4 — implement Linux native register transition and scoped fault recovery, including recognized gate-stop capture.
3. #14 — add bounded guest-memory access plus `astraea.test.write` and `astraea.test.exit`.
4. #15 — execute the first end-to-end `probe_hello.elf`.
5. #5 — implement the equivalent guarded Windows x86-64 backend.

## Execution boundary

Native execution v0 remains limited to trusted Astraea-owned synthetic probes.

- Exact guest-address mappings.
- No-replace host mapping semantics.
- Strict W^X.
- Dedicated execution thread.
- Scoped fault recovery.
- No C++ unwinding through guest frames.
- Portable guest CPU state.
- Synthetic HLE gates independent of guessed PS5 NIDs.

Arbitrary retail guest execution is not enabled.

## Clean-room boundary

Do not commit Sony firmware, Sony keys, proprietary SDK material, decrypted
retail game assets, copyrighted PS5 executables, proprietary system modules,
DRM-bypass material, or unrelated employer/proprietary material.

PS5-specific assumptions require documented evidence.

## Research / verification backlog

- #6 stable Astraea trace schema v0.
- #7 trace diff and first-divergence locator.
- #8 public PS5 executable/module ABI evidence map.
- #9 RDNA2/PS5 graphics evidence map.
- #10 AstraeaProbe v0 controlled behavioral probe format.

## Next action

Implement #13 as the portable/runtime foundation required by #4, then complete
Linux transition/fault recovery, bounded guest-memory services, and #15
`probe_hello.elf`.
