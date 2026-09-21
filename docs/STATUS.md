# Project Status

**Milestone:** M2 — Controlled execution  
**State:** public clean-room baseline; portable execution core complete  
**Repository:** astraea-emu/astraea  
**Active branch:** `main`

## Complete
- M0 engineering foundation.
- M1 validated `GuestImage`.
- M2 guarded native x86-64 execution architecture.
- M2 portable `GuestCpuContext`, backend stop/fault model, and execution-memory planner.
- Five-gate validation completed before the public clean-room migration: Linux x64, Windows x64, macOS ARM64, Linux ASan + UBSan, and Linux Clang fuzz smoke.

## Current frontier
1. Linux exact-address guest memory preparation and teardown.
2. Linux native register transition and scoped fault recovery.
3. Synthetic host-gate / HLE dispatch.
4. Bounded guest-memory access.
5. `astraea.test.write` and `astraea.test.exit`.
6. First end-to-end `probe_hello.elf`.

The memory-preparation implementation and host-gate/HLE design from the private development archive will be recreated here as new pseudonymous branches and commits rather than importing prior Git history.

## Execution boundary
Native execution v0 remains limited to trusted Astraea-owned synthetic probes. Exact guest-address mappings, no-replace semantics, strict W^X, dedicated execution threads, scoped fault recovery, and portable CPU state remain mandatory. Arbitrary retail guest execution is not enabled.

## Clean-room boundary
Do not commit Sony firmware, Sony keys, proprietary SDK material, decrypted retail game assets, copyrighted PS5 executables, proprietary system modules, DRM-bypass material, or unrelated employer/proprietary material. PS5-specific assumptions require documented evidence.

## Next action
1. Verify public CI from this clean baseline.
2. Recreate Linux exact-address memory-preparation work on a clean branch.
3. Recreate synthetic host-gate/HLE specification work.
4. Validate and merge Linux memory preparation.
5. Implement native register transition and scoped fault recovery.
6. Reach the first controlled `probe_hello.elf`.
