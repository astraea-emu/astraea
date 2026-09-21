# Project Status

**Milestone:** M2 — Controlled execution  
**State:** Linux end-to-end controlled execution complete; Windows x86-64 parity in review  
**Repository:** astraea-emu/astraea  
**Active branch:** `feat/m2-windows-native-backend`

## Complete

- M0 engineering foundation.
- M1 validated `GuestImage`.
- M2 portable execution context/backend/memory-planning contracts.
- Linux exact-address guest memory preparation and teardown (#2).
- Synthetic host-gate / HLE dispatch ABI (#3).
- Synthetic HLE registry and gate-region model (#13).
- Linux native register transition and scoped fault recovery (#4).
- Bounded guest-memory access and synthetic `test.write` / `test.exit` (#14).
- Astraea-owned `probe_hello.elf` end-to-end proof (#15):
  - strict ELF / `GuestImage` validation
  - exact-address Linux memory preparation
  - native x86-64 entry
  - host gate -> `test.write`
  - guest-side RAX validation after resume
  - exact output `Hello from guest`
  - host gate -> `test.exit(42)`
  - deterministic repeated execution
  - structured execution events
- Public five-gate CI remains the merge requirement.

## Current frontier

1. #5 — guarded Windows x86-64 native backend — in review on this branch.
2. Prove Windows exact-address preparation, W^X, gate entry/recovery, GPR capture, access/illegal faults, and deterministic teardown under the public Windows runner.
3. After Windows parity, close M2 controlled-execution foundation and begin the evidence-driven PS5-facing layer:
   - #8 executable/module ABI evidence
   - #10 AstraeaProbe v0 format
   - #6/#7 trace schema and divergence tooling
   - platform import/HLE work only from evidence

## Windows #5 scope

- exact guest-address reservation using Win32 virtual memory primitives
- allocation-granularity-aware reservations without replacing existing mappings
- staged RW population followed by final W^X protections
- instruction-cache synchronization
- Windows x64 transition thunk preserving required host ABI state
- portable `GuestCpuContext` installation/capture
- vectored exception recovery for guest access, illegal-instruction, and arithmetic faults
- exact synthetic gate recognition
- unrelated process exceptions are not consumed
- deterministic memory/gate/handler teardown
- trusted Astraea-owned synthetic probes only

## Execution boundary

Native execution remains limited to trusted Astraea-owned synthetic probes.
Arbitrary retail guest execution is not enabled.

## Clean-room boundary

Do not commit Sony firmware, Sony keys, proprietary SDK material, decrypted
retail game assets, copyrighted PS5 executables, proprietary system modules,
DRM-bypass material, or unrelated employer/proprietary material.

PS5-specific assumptions require documented evidence.

## Next action

Run #5 through the public five-gate matrix. Fix Windows-specific compiler/runtime
issues without weakening the Linux/macOS portability gates, then merge only when
the controlled Windows cases are green.
