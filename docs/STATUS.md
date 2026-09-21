# Project Status

**Milestone:** M2 — Controlled execution  
**State:** Linux end-to-end controlled execution proof complete; Windows parity remains  
**Repository:** astraea-emu/astraea  
**Active branch:** `main`

## Complete

- M0 engineering foundation.
- M1 validated `GuestImage`.
- M2 guarded native x86-64 execution architecture.
- M2 portable `GuestCpuContext`, backend stop/fault model, and execution-memory planner.
- Linux exact-address guest memory preparation and teardown (#2).
- Synthetic host-gate / HLE dispatch ABI specification (#3).
- Synthetic HLE registry and gate-region model (#13).
- Linux native register transition and scoped fault recovery (#4).
- Bounded guest-memory access and synthetic `test.write` / `test.exit` services (#14).
- Full Linux x86-64 register re-entry, including HLE return values in RAX.
- First Astraea-owned ELF64 probe executed end to end (#15):
  - strict M1 ELF / `GuestImage` validation
  - exact-address Linux memory preparation
  - guarded native x86-64 entry
  - `astraea.test.write`
  - guest-side RAX verification after resume
  - exact output `Hello from guest`
  - `astraea.test.exit(42)`
  - deterministic repeated execution
  - structured guest-entry, gate-stop, HLE-resume, and HLE-exit events
- Public five-gate CI:
  - Linux x64
  - Windows x64
  - macOS ARM64
  - Linux ASan + UBSan
  - Linux Clang fuzz smoke

## Current frontier

1. #5 — implement the equivalent guarded Windows x86-64 native backend.
2. Run the same controlled synthetic entry, gate, GPR, fault, and teardown cases on Windows.
3. After Linux/Windows controlled execution parity, expand synthetic probes and begin evidence-driven platform HLE.
4. Build the later trace/diff/replay layer on top of the structured execution evidence rather than adding ad hoc logging.

## Controlled execution proof

The current Linux proof is a complete:

`ELF bytes -> strict loader -> GuestImage -> exact guest memory -> native guest entry -> host gate -> HLE -> validated resume -> host gate -> exit`

The owned probe deliberately executes `UD2` if `test.write` returns the wrong
value in RAX or if `test.exit` incorrectly resumes, preventing a false-positive
end-to-end result.

## Execution boundary

Native execution remains limited to trusted Astraea-owned synthetic probes.
Arbitrary retail guest execution is not enabled.

## Clean-room boundary

Do not commit Sony firmware, Sony keys, proprietary SDK material, decrypted
retail game assets, copyrighted PS5 executables, proprietary system modules,
DRM-bypass material, or unrelated employer/proprietary material.

PS5-specific assumptions require documented evidence.

## Next action

Implement #5 using the same portable context, memory-safety, host-gate, and
fault-normalization contracts already proven on Linux.
