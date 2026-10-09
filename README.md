# Astraea

[![CI](https://github.com/astraea-emu/astraea/actions/workflows/ci.yml/badge.svg)](https://github.com/astraea-emu/astraea/actions/workflows/ci.yml)

**A verification-first, clean-room PlayStation 5 compatibility research emulator.**

Astraea explores PS5 executable/runtime behavior, native x86-64 execution,
AGC/RDNA2 graphics, and Vulkan translation with an evidence-first rule:
unknown platform behavior stays explicit instead of becoming a compatibility
guess.

> **Research status:** Astraea is not yet a commercial-title emulator.
> Linux x86-64 can safely admit a legally obtained executable to a supervised
> diagnostic path, but a structurally ready title still stops before its first
> retail instruction while the remaining PS5 process-entry state is established.

Astraea is an independent project and is not affiliated with or endorsed by
Sony Interactive Entertainment.

## Current frontier

| Area | State |
| --- | --- |
| Graphics V0-V3 | **Complete for bounded owned workloads** — AGC/RDNA2 input through typed guest GPU state to deterministic Vulkan execution/readback |
| Retail C0 | **Complete on Linux x86-64** — supervised sealed-artifact diagnostic with typed first-boundary reporting |
| Retail C1 | **Active** — corroborated startup-block prefix and process-entry observation validator are merged; RSI/RSP, procparam, TLS/TCB, and pre-entry bootstrap evidence remain |
| Verification S0 | **Established** — multi-platform CI, ASan/UBSan, fuzz smoke, traces, probes, typed unsupported behavior, ADR/provenance discipline |
| Commercial game boot/playability | **Not claimed** |

The exact merged frontier and next dependency live in
[`docs/STATUS.md`](docs/STATUS.md).

## What exists today

### Loader and execution

- strict PS5/SCE ELF parsing and mapping validation;
- dynamic metadata, symbol, relocation, import-identity, and TLS-template foundations;
- controlled native x86-64 guest execution for Astraea-owned probes;
- Linux and Windows owned native-execution/supervisor proofs;
- typed HLE/import gate infrastructure;
- a corroborated partial direct-title process-entry contract.

### Supervised retail diagnostics

Linux x86-64 has a production diagnostic path with:

- separate controller and untrusted worker processes;
- sealed artifact handoff instead of passing the original host pathname;
- finite wall-clock and kernel resource ceilings;
- pidfd-backed worker identity/signalling when available;
- pre-kernel seccomp interception for guest-originated raw `SYSCALL`;
- typed syscall/fault/diagnostic boundaries;
- deterministic loader, dependency, relocation, TLS, and process-entry stops.

A currently otherwise-ready image stops at
`unsupported_initial_process_abi`. That is an intentional correctness
boundary, not a boot failure hidden behind a success stub.

### Graphics

The bounded graphics path proves:

```text
AGC shader/container
  -> generic RDNA2 decode
  -> semantic Shader IR
  -> compiler/value IR
  -> Vulkan-valid SPIR-V

submitted PM4/state/resources
  -> typed guest GPU state
  -> shader/resource resolution
  -> real Vulkan raster/transfer execution
  -> deterministic readback
```

This does **not** imply complete PM4, AGC, shader ISA, descriptors, tiling,
resource tracking, synchronization, or presentation support.

## Architecture

Astraea keeps guest semantics separate from host implementation:

1. **Guest image / process** — SCE ELF, mappings, relocations, imports, process-entry state, TLS and native x86-64 execution.
2. **Supervised runtime** — worker isolation, syscall interception, faults, finite resource policy and typed stops.
3. **PS5 GPU frontend** — AGC objects, PM4, registers, resources and synchronization.
4. **Shader semantics** — AMD-documented RDNA2 decoding into a guest-semantic Shader IR.
5. **Compiler/backend** — workload-driven compiler IR, SPIR-V and Vulkan.
6. **Verification** — deterministic fixtures, trace/first-divergence tooling, AstraeaProbe, sanitizers, fuzzing and controlled hardware evidence.

The full architecture and gate definitions are in
[`docs/PROJECT_PLAN.md`](docs/PROJECT_PLAN.md) and [`docs/adr/`](docs/adr/).

## Roadmap

Astraea tracks three orthogonal axes:

- **V — graphics technology:** bounded graphics integration and controlled differential validation;
- **C — compatibility:** diagnostic -> first retail instruction -> first deterministic post-entry divergence -> runtime closure -> boot -> headless frame -> presentation -> in-game/playable -> reference-guarded support;
- **S — scalability/readiness:** first-divergence coverage, cross-title regression guards, architecture ratchets, measured performance, and eventual release quality.

The current critical path is **C1, software-first**. The first
independently authored PS5-format title is pinned in CI and reaches an
explicit pre-entry `unsupported_dynamic_dependencies` stop in Astraea's
supervised diagnostic. Its read-only dependency manifest now identifies
exact module names, source-attributed SCE symbol IDs and relocation demand.
A second **source-generated, non-Sony PRX** is also pinned in CI, with
2,669 dynamic symbols, 1,896 relocations and nonempty TLS metadata.
The generic owned-code linker, mapped-memory and relocation primitives
continue to expand under their own negative tests. None of this is
commercial guest execution, PS5 firmware ABI evidence or gameplay.

For the exact PC-first gating sequence and the deliberately delayed firmware-13.00 hardware experiment, read the [C1 execution and hardware readiness plan](docs/research/c1_pc_first_execution_hardware_readiness_2026-10-09.md).

The #334 two-run real-PS5 normal-title startup observation remains required
later, before promoting the selected real-title process-entry profile (#348).
The first post-entry milestone is C2.0: a typed reproducible retail
divergence, not 'boot'. See [current status](docs/STATUS.md), the
[governing C1 sequence](docs/PROJECT_PLAN.md), and
[public native-title closure evidence](docs/research/public_native_title_closure_2026-10-08.md).

See [`docs/README.md`](docs/README.md) for the documentation map.

## Build and test

Requirements include CMake 3.25+, a C++23 compiler, and the platform tools
needed by the selected preset.

Linux:

```sh
cmake --preset linux-dev
cmake --build --preset linux-dev
ctest --preset linux-dev
```

macOS:

```sh
bash scripts/bootstrap-macos.sh
cmake --preset macos-dev
cmake --build --preset macos-dev
ctest --preset macos-dev
```

Windows development uses the `windows-dev` CMake preset.

CI gates every pull request on Linux x64, Windows x64, macOS ARM64, Linux
ASan+UBSan, and Linux Clang fuzz smoke.

### Static retail closure profile

On any supported development host, Astraea can perform read-only structural
analysis of a locally supplied PS5/SCE executable without executing guest
instructions:

```sh
./out/build/linux-dev/astraea profile /path/to/artifact
```

Use the equivalent built `astraea` binary on Windows or macOS.

The command reports independent structural-pressure dimensions such as module/
library dependencies, relocations, mapped/executable footprint, symbol-table
size and TLS requirements. It deliberately does **not** collapse them into a
compatibility percentage or weighted difficulty score.

For additional *read-only* exact metadata on lawfully held SCE ELF inputs:

```sh
./out/build/linux-dev/astraea dependencies /path/to/artifact
```

The default manifest preserves opaque SCE dynamic records, losslessly
hex-encodes untrusted dependency and symbol names, and counts referenced
relocations. The **opt-in** `dependencies --public-sce-pack-v1` mode
experiments with one independently authored public-linker field encoding;
it must not be interpreted as a universal PS5 ABI. See
[the second clean-room PRX corpus](docs/research/public_cleanroom_prx_second_corpus_2026-10-08.md)
and [the current status](docs/STATUS.md).

`profile` and the Linux `diagnose` path share one bounded artifact reader
and one deterministic non-overlapping analysis-stack policy, so title-selection
analysis cannot silently drift from production preflight behavior.

### Linux retail diagnostic

On Linux x86-64, a locally built Astraea binary can inspect a legally obtained
artifact through the supervised diagnostic boundary:

```sh
./out/build/linux-dev/astraea diagnose /path/to/artifact
```

This is a diagnostic/research interface. It does not imply that the title will
boot or that Astraea supports redistributed game content.

## Clean-room boundary

This repository does not include or request Sony source code, firmware, keys,
proprietary SDK files, proprietary system modules, or retail game content.

PS5-specific behavior must be supported by public specifications, lawful
interface information, independently authored tooling, or controlled
observations with provenance. Comparative emulator implementations are useful
for experiment design, but are not treated as hardware truth.

Read [`docs/CLEAN_ROOM.md`](docs/CLEAN_ROOM.md) before contributing
PS5-specific behavior.

## Contributing

Contributions should be small, testable, provenance-aware, and tied to a real
dependency or invariant. See [`CONTRIBUTING.md`](CONTRIBUTING.md).

## License

GNU General Public License v3.0 or later. See [`LICENSE`](LICENSE).
