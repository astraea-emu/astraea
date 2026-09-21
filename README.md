# Astraea

A verification-first PlayStation 5 compatibility research and emulation project.

> **Status:** M3 behavioral evidence and differential tooling is active. M2 controlled execution is complete for trusted Astraea-owned x86-64 probes on Linux and Windows. Astraea does not currently run PlayStation 5 software.

## Principles

- Clean-room implementation: no proprietary Sony source code, firmware, keys, SDK files, or copyrighted game assets in this repository.
- Evidence before emulation: document observed or public behavior before encoding platform-specific assumptions.
- Native x86-64 execution where host architecture permits it; portable subsystems remain host-independent.
- Verification-first development: structured traces, differential tests, regression localization, fuzzing, sanitizers, and reproducible experiments.
- Small reviewed changes: every implementation task should have explicit scope and acceptance criteria.
- GitHub is the durable source of truth for architecture, status, decisions, and handoffs.

## Initial architecture

Astraea is organized around five major concerns:

1. **Guest image and loader** — ELF/module parsing, mappings, relocations, imports, TLS, and stack setup.
2. **Execution and HLE** — guest CPU context, guest/host transitions, exceptions, and high-level emulation of system interfaces.
3. **Graphics** — PS5/RDNA2 command and shader understanding, an Astraea graphics IR, and host backends beginning with Vulkan.
4. **Astraea Lab** — trace capture, normalization, diffing, replayable portions, regression minimization, and behavioral corpora.
5. **Astraea Probe** — controlled research programs and experiments for legally obtained/reference hardware when appropriate.

## Development hosts

The project is designed to be developed from macOS, Linux, and Windows. The initial primary runtime targets for native x86-64 guest execution will be x86-64 Linux and Windows. macOS remains a first-class development host, but Apple Silicon cannot be treated as equivalent to an x86-64 runtime host.

See:
- `docs/PROJECT_PLAN.md`
- `docs/DEVELOPMENT_MACOS.md`
- `docs/CHAT_HANDOFF.md`

## Current milestone

**M3 — Behavioral evidence and differential tooling**

M2's validated ELF/guest-image pipeline, guarded Linux and Windows x86-64 execution backends, bounded synthetic HLE path, and `probe_hello.elf` proof are complete. M3 now includes the public PS5 executable/module ABI evidence map, AstraeaProbe v0, Trace v0 normalization/serialization, deterministic first-divergence tooling, and the RDNA2/PS5 graphics evidence map.

The current implementation frontier is deliberately narrow: typed graphics-frontend parsing with raw provenance, evidence-backed SCE metadata parsing, a generic RDNA2 decoder, minimal Graphics IR / Shader IR contracts, and Trace v0 adapters before any Vulkan backend work.

No compatibility claims should be made until the corresponding behavior is implemented and covered by tests.

## License

Astraea is licensed under the GNU General Public License v3.0 or later. See `LICENSE`.

