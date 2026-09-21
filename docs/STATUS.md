# Project Status

**Milestone:** M3 — Behavioral evidence and differential tooling  
**State:** Trace v0 merged and green; deterministic first-divergence diff in review  
**Repository:** astraea-emu/astraea  
**Active branch:** `feat/m3-trace-first-divergence`

## Complete

- M0 engineering foundation.
- M1 validated `GuestImage`.
- M2 controlled execution on Linux x86-64 and Windows x86-64 for trusted Astraea-owned synthetic probes.
- Astraea-owned `probe_hello.elf` end-to-end proof.
- Public PS5 executable/module ABI evidence map (#8).
- AstraeaProbe v0 request/result contract and deterministic host reference probe (#10).
- Stable Astraea Trace v0 schema, normalization, and canonical serializer (#6).
- Public five-gate CI remains the merge requirement:
  - Linux x64
  - Windows x64
  - macOS ARM64
  - Linux ASan + UBSan
  - Linux Clang fuzz smoke

## Current frontier

1. #7 — deterministic Trace v0 diff / first-divergence locator — in review on this branch.
2. #9 — RDNA2/PS5 graphics evidence map before serious GPU implementation.
3. Evidence-backed SCE metadata parsing/import work derived from #8 and later controlled observations.
4. Automatic runtime-event -> Trace v0 adapters only after the stable diff contract is proven.

## #7 scope

- normalize both Trace v0 inputs before comparison
- compare stable run metadata exactly
- exclude provenance and diagnostics from default behavioral comparison
- align events by subsystem, type, and normalized guest location
- compare event ids by default with an explicit opt-out after semantic alignment
- compare normalized stable fields and typed values
- explicit scoped ignore rules for known nondeterministic stable fields
- deterministic insertion/deletion detection via forward semantic re-alignment
- typed first-divergence result for regression automation
- human-readable report generated from the typed result
- tests for value mismatch, insertion, deletion, identity mismatch, ignored fields, event-id policy, malformed input, and diagnostics/provenance exclusion

This slice does not parse arbitrary trace JSON, globally minimize differences, or
automatically adapt M2 runtime events into Trace v0.

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

Validate #7 across the five-gate matrix. Once the deterministic first-divergence
contract is merged, continue #9 and then connect controlled probe/trace evidence
to evidence-backed platform metadata work without guessing PS5 behavior.
