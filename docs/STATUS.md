# Project Status

**Milestone:** M2 — Controlled execution  
**State:** complete on Linux x86-64 and Windows x86-64 for trusted Astraea-owned synthetic probes  
**Repository:** astraea-emu/astraea  
**Active branch:** `main`

## M2 complete

- M0 engineering foundation.
- M1 validated `GuestImage`.
- Portable `GuestCpuContext`, backend stop/fault model, and execution-memory planner.
- Exact-address guest-memory preparation on Linux and Windows.
- Strict W^X staging/final protection on both native x86-64 hosts.
- Guarded native x86-64 register transition and fault/exception recovery on Linux and Windows.
- Synthetic host-gate / HLE dispatch ABI.
- Synthetic HLE registry and deterministic gate-region model.
- Bounded guest-memory reads/writes and synthetic `test.write` / `test.exit`.
- Linux `probe_hello.elf` end-to-end proof:
  - strict ELF / `GuestImage` validation
  - exact-address memory preparation
  - native x86-64 entry
  - host gate -> `test.write`
  - guest-side RAX validation after resume
  - exact output `Hello from guest`
  - host gate -> `test.exit(42)`
  - deterministic repeated execution
  - structured execution events
- Windows x86-64 parity:
  - allocation-granularity-aware reservations
  - no-clobber collision handling
  - staged RW population -> final W^X
  - MASM transition/recovery thunk
  - vectored exception recovery
  - gate-stop capture
  - illegal/access fault normalization
  - guest GPR capture and host-state restoration
  - deterministic teardown/re-entry
- Public five-gate CI remains the merge requirement:
  - Linux x64
  - Windows x64
  - macOS ARM64
  - Linux ASan + UBSan
  - Linux Clang fuzz smoke

## Current frontier

The next work is evidence-first. Do not encode guessed PS5 behavior merely because
the generic execution substrate now works.

1. #8 — build the public PS5 executable/module ABI evidence map.
2. #10 — specify AstraeaProbe v0 for controlled behavioral experiments.
3. #6 — define stable Astraea trace schema v0.
4. #7 — implement trace diff / first-divergence location on top of #6.
5. #9 — continue the RDNA2/PS5 graphics evidence map before serious GPU implementation.
6. Only then expand platform import/module resolution and HLE from documented evidence.

## Execution boundary

Native execution remains limited to trusted Astraea-owned synthetic probes.

Astraea does **not** currently claim PlayStation 5 software compatibility, and
arbitrary retail guest execution is not enabled.

## Clean-room boundary

Do not commit Sony firmware, Sony keys, proprietary SDK material, decrypted
retail game assets, copyrighted PS5 executables, proprietary system modules,
DRM-bypass material, or unrelated employer/proprietary material.

PS5-specific assumptions require documented evidence.

## Next action

Start #8 by collecting and classifying public evidence for PS5 executable/module
identity, import/export representation, module/library naming, and unresolved
questions. Keep implementation changes out of that research slice unless the
evidence justifies them.
