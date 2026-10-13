# Astraea

[![CI](https://github.com/astraea-emu/astraea/actions/workflows/ci.yml/badge.svg)](https://github.com/astraea-emu/astraea/actions/workflows/ci.yml)

**A verification-first, clean-room PlayStation 5 compatibility research emulator written in C++23.**

Astraea studies PS5 executable loading, guest CPU execution, PS5 AGC/RDNA2 graphics and Vulkan translation. Unsupported platform behavior remains visible rather than being replaced with unverified compatibility assumptions.

> **Development status:** This is **not a playable PS5 emulator**. No commercial title has executed a first guest instruction or produced a title-generated frame in Astraea. The production Linux diagnostic is deliberately pre-entry; separately admitted source-owned test binaries and one independent homebrew analysis do not constitute retail compatibility.

Astraea is independent and is not affiliated with or endorsed by Sony Interactive Entertainment.

## Where the project stands

| Area | Currently verified |
| --- | --- |
| Loader / C0 | Bounded PS5/SCE ELF parsing, dynamic metadata, relocations, mappings and supervised read-only/pre-entry diagnostics on Linux x86-64 |
| CPU / C1 research | Source-owned native x86-64 instruction/fault observations; thirteen independent Breakout instructions interpreted **non-natively** to an unsupported imported CALL |
| Native security | Kernel-observed guest/non-guest memory distinction and a restricted source-owned native write gate; **general arbitrary-code containment remains open** |
| Graphics / V0–V3 | Bounded owned AGC/RDNA2 → typed shader/state → SPIR-V/Vulkan execution/readback; **no title-produced frame** |
| Verification | Linux x64, Windows x64, macOS ARM64, ASan/UBSan and fuzz smoke plus two pinned C1 workflows |
| Playable titles | None demonstrated |

See **[Live Project Status](docs/STATUS.md)** for the exact merged frontier, blocked dependencies and current priority. The comprehensive [project plan](docs/PROJECT_PLAN.md) defines the C0–C6, V0–V5 and S0–S5 milestones; do not confuse passing isolated research tests with completing a compatibility milestone.

## Selected independent homebrew: Breakout

The unchanged publicly authored [SharpEmu Breakout demo](https://github.com/sharpemu/sharpemu-demo) is the current software-first integration target. The source-pinned PS5-marked ELF has one `RELATIVE` and 19 `GLOB_DAT` relocations, with all 19 imports still **unbound**. Under an explicitly synthetic CPU/stack context, Astraea interprets 13 genuine source-backed instructions, stopping **before** the first unsupported indirect CALL at RIP `0x23c`, GOT `0x68c8`, bare NID `Vo5V8KAwCmk`.

Later static-only analysis observed four compiled fallthrough instructions before the next control-flow boundary, but **did not execute the import or those instructions**. A separate host-compiled Breakout canvas is a reference oracle, **not** guest-executed rendering. [Integration issue #378](https://github.com/astraea-emu/astraea/issues/378) tracks the first real executable/service boundary.

## Architecture

```text
Authorized executable → checked GuestImage / relocations / guest virtual memory
                               ↓
           CPU process context / controlled execution / typed stop
                               ↓
               verified service identity / HLE or LLE boundary
                               ↓
                guest GPU commands, resources, synchronization
                               ↓
              RDNA2 / Shader IR → SPIR-V → Vulkan backend
                               ↓
                        readback / presentation

     separate trusted controller / untrusted worker / diagnostic gates
```

The CPU strategy is still under active review: direct native acceleration is attractive on x86-64 but requires **enforceable isolation of host memory and control flow**; a mediated CPU backend may be needed for safe and portable execution. Astraea does **not** currently admit arbitrary native Breakout or retail code. See [security issue #410](https://github.com/astraea-emu/astraea/issues/410) and [accepted ADRs](docs/adr/README.md).

## Build and test

Requires CMake 3.25+, a C++23 compiler and platform dependencies described by the selected preset.

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

Windows uses the `windows-dev` CMake preset. The project's CI runs the five general configurations noted above and separate pinned C1 research proofs. A successful build is not evidence of gameplay.

## Diagnostic-only interfaces

A locally authorized artifact can be inspected **without executing its guest instructions**:

```sh
./out/build/linux-dev/astraea profile /path/to/artifact
./out/build/linux-dev/astraea dependencies /path/to/artifact
```

On Linux x86-64, supervised pre-entry diagnostics are also available:

```sh
./out/build/linux-dev/astraea diagnose /path/to/artifact
```

Use the equivalent built executable on macOS/Windows for portable structural analysis. These commands report bounded metadata or explicit unsupported states; they do not promise to boot a title. Opt-in public-linker/raw-ELF interpretation remains separate from authenticated Sony ABI behavior. Details and limitations: [documentation index](docs/README.md), [live status](docs/STATUS.md) and [research notes](docs/research/README.md).

## Clean-room, contribution and licensing

- **Never commit:** Sony firmware, encryption keys, proprietary system modules/SDK materials, decrypted retail executables, proprietary game assets or unsafe access-control bypass material.
- **Guest-visible PS5 semantics require evidence:** independently authored sources, lawful public interface information or controlled measurements. Third-party emulator behavior is comparative evidence, not automatically platform truth.
- **Contribution standard:** one scoped dependency, explicit permitted/prohibited behavior, ownership and provenance, positive/negative tests, final exact-head CI. See [CONTRIBUTING](CONTRIBUTING.md) and [clean-room policy](docs/CLEAN_ROOM.md).
- **License:** GPL-3.0-or-later; see [LICENSE](LICENSE).
