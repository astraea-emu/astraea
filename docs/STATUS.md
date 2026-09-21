# Project Status

**Milestone:** M3 — Behavioral evidence and differential tooling  
**State:** AstraeaProbe v0 complete; stable Trace v0 schema/serializer in review  
**Repository:** astraea-emu/astraea  
**Active branch:** `spec/m3-trace-v0`

## Complete

- M0 engineering foundation.
- M1 validated `GuestImage`.
- M2 controlled execution on Linux x86-64 and Windows x86-64 for trusted Astraea-owned synthetic probes.
- Astraea-owned `probe_hello.elf` end-to-end proof.
- Public PS5 executable/module ABI evidence map (#8).
- AstraeaProbe v0 request/result contract and deterministic host reference probe (#10).
- Public five-gate CI remains the merge requirement:
  - Linux x64
  - Windows x64
  - macOS ARM64
  - Linux ASan + UBSan
  - Linux Clang fuzz smoke

## Current frontier

1. #6 — stable Astraea Trace v0 schema, normalization, and canonical serialization — in review on this branch.
2. #7 — trace diff / first-divergence locator built on the stable #6 contract.
3. #9 — RDNA2/PS5 graphics evidence map before serious GPU implementation.
4. Evidence-backed SCE metadata parsing/import work derived from #8 and later controlled observations.

## #6 scope

- schema/versioned compact JSON trace document
- strictly increasing per-trace event identity
- subsystem/event-type semantic identifiers
- normalized guest identity as stable guest object + relative offset
- explicit stable-field versus diagnostic-field classification
- run metadata and provenance without host pointers
- deterministic normalization of field maps and artifact-digest sets
- canonical u64/bytes lexical forms
- UTF-8 validation for stable text
- deterministic serializer with exact reference bytes
- committed synthetic four-event trace example
- typed failures for malformed identity, duplicate fields, invalid digests, and non-monotonic events

This slice is serializer-only. It does not yet parse arbitrary trace JSON,
automatically adapt M2 events, compute trace digests, or implement #7 diffing.

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

Validate #6 across the five-gate matrix. After the stable trace envelope is
merged, implement #7's deterministic first-divergence locator against the
stable projection rather than raw host/runtime diagnostics.
