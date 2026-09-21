# Project Status

**Milestone:** M3 — Behavioral evidence and differential tooling  
**State:** M2 controlled execution complete; AstraeaProbe v0 specification in review  
**Repository:** astraea-emu/astraea  
**Active branch:** `spec/astraea-probe-v0`

## Complete

- M0 engineering foundation.
- M1 validated `GuestImage`.
- M2 controlled execution on Linux x86-64 and Windows x86-64 for trusted Astraea-owned synthetic probes.
- Astraea-owned `probe_hello.elf` end-to-end proof with deterministic HLE write/resume/exit behavior.
- Public PS5 executable/module ABI evidence map (#8), with provenance/confidence separation and unresolved questions retained explicitly.
- Public five-gate CI remains the merge requirement:
  - Linux x64
  - Windows x64
  - macOS ARM64
  - Linux ASan + UBSan
  - Linux Clang fuzz smoke

## Current frontier

1. #10 — AstraeaProbe v0 controlled behavioral probe format — in review on this branch.
2. #6 — stable Astraea trace schema v0.
3. #7 — trace diff / first-divergence locator built on #6.
4. #9 — RDNA2/PS5 graphics evidence map before serious GPU implementation.
5. Platform import/module resolution and HLE only where #8 or later controlled evidence justifies behavior.

## #10 scope

- stable probe identity and independent probe version
- explicit typed input/observation values
- transport-neutral request/result JSON contracts
- case identity independent of runner
- full-request identity including semantic environment/provenance
- deterministic canonicalization rules and committed SHA-256 test vectors
- behavioral projection excluding host/transport noise
- reproducibility, timeout, fault-normalization, and provenance rules
- clean boundary around optional lawful reference-hardware adapters
- deterministic host-only `astraea.reference.echo` reference runner
- no jailbreak, authentication-bypass, firmware-key, or proprietary-module dependency in core

The current implementation slice does **not** yet implement a general v0 JSON
parser, canonical serializer, or digest engine. The committed vectors specify
their required future behavior; the host echo runner tests only deterministic
reference-probe semantics.

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

Validate #10 as a specification/reference-runner slice, then define #6's stable
trace schema independently. Do not make Probe v0 depend on an unfinished trace
serialization format.
