# Project Status

**Milestone:** M2 — Controlled execution  
**State:** public clean-room baseline green; bounded guest memory and synthetic HLE services in review  
**Repository:** astraea-emu/astraea  
**Active branch:** `feat/m2-bounded-hle-services`

## Complete

- M0 engineering foundation.
- M1 validated `GuestImage`.
- M2 guarded native x86-64 execution architecture.
- M2 portable `GuestCpuContext`, backend stop/fault model, and execution-memory planner.
- Linux exact-address guest memory preparation and teardown (#2).
- Synthetic host-gate / HLE dispatch ABI specification (#3).
- Synthetic HLE registry and gate-region model (#13).
- Linux native register transition and scoped fault recovery (#4).
- Public five-gate CI:
  - Linux x64
  - Windows x64
  - macOS ARM64
  - Linux ASan + UBSan
  - Linux Clang fuzz smoke

## Current frontier

1. #14 — bounded guest-memory access plus `astraea.test.write` and `astraea.test.exit` — in review on this branch.
2. #15 — first end-to-end `probe_hello.elf`.
3. #5 — equivalent guarded Windows x86-64 backend.

## #14 scope

- exact checked live guest-memory reads/writes
- mapping and permission validation before host dereference
- checked guest-range arithmetic and host-size handling
- bounded C-string reads
- synthetic `test.write` transcript capture
- synthetic `test.exit`
- HLE call materialization only after safe signal recovery
- validated guest return-address read
- exact executable return-RIP validation
- checked guest RSP advancement
- host-side Linux synthetic-session resume/exit loop

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

Validate #14 across the public five-gate matrix. After merge, build #15 as an
Astraea-owned ELF fixture and execute `probe_hello.elf` end to end.
