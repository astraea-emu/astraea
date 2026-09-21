# Project Status

**Milestone:** M2 — Controlled execution  
**State:** public clean-room baseline green; Linux memory preparation in review  
**Repository:** astraea-emu/astraea  
**Active branch:** `feat/m2-linux-memory-prep-clean`

## Complete

- M0 engineering foundation.
- M1 validated `GuestImage`.
- M2 guarded native x86-64 execution architecture.
- M2 portable `GuestCpuContext`, backend stop/fault model, and execution-memory planner.
- Public clean-room baseline migrated without prior Git history.
- Public five-gate CI is green:
  - Linux x64
  - Windows x64
  - macOS ARM64
  - Linux ASan + UBSan
  - Linux Clang fuzz smoke

## Current frontier

- #2 Linux exact-address guest memory preparation and teardown — in review on this branch.
- #3 synthetic host-gate/HLE dispatch ABI — next design branch.
- #4 Linux native register transition and scoped fault recovery — follows #2 and #3.
- First end-to-end target: `probe_hello.elf`.

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

## Next action

1. Validate and merge #2 after public CI/review.
2. Recreate and validate #3.
3. Implement #4.
4. Add bounded guest-memory access and synthetic `test.write` / `test.exit`.
5. Reach the first controlled `probe_hello.elf` execution.
