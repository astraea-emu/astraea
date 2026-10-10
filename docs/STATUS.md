# Project Status

**As of 2026-10-10 (after #404, #406 and #408 merged):** C0 Linux retail diagnostics and bounded V0–V3 graphics are merged. C1 is active. **No commercial PS5 game has entered its first guest instruction, booted, rendered a title-generated frame or reached gameplay in Astraea.**

This page records the current merged frontier and the next dependency. The [project plan](PROJECT_PLAN.md) defines the durable C/V/S gates; [research notes](research/README.md) preserve dated evidence and falsification records. Historical PR details belong in their PRs and linked research notes, not in this live summary.

## Milestone summary

| Axis | Status | Scope |
| --- | --- | --- |
| M0–M2 | Complete | Build foundation, checked guest image and controlled Astraea-owned native execution |
| M3 / S0 | Established | Typed failures, provenance, CI, sanitizers, parser fuzz smoke, traces and probes |
| V0–V3 | Complete **only for bounded owned workloads** | AGC/RDNA2 input → guest GPU state / shader IR → validated SPIR-V → deterministic Vulkan execution/readback |
| C0 | Complete **on Linux x86-64** | Sealed-artifact, process-supervised retail diagnostic and typed pre-entry stop |
| C1 | **Active; not complete** | Evidenced title entry and first real retail instruction |
| C2.0–C6 / S1–S5 | Not achieved as title/release milestones | Post-entry divergence, sustained boot, title frames, menu, gameplay, validated support and release quality |

## Verified implementation

- **Loader and image:** checked PS5/SCE ELF image, mapped segments, dynamic metadata, symbol/identity inventories, relocation descriptors, initial-stack/TLS metadata, strict bounds and unsupported classifications.
- **Owned execution:** controlled x86-64 guest probes on Linux/Windows. Linux includes a separate supervised worker, sealed input handoff, finite resource limits, pre-kernel guest `SYSCALL` containment, typed fault/syscall events and deterministic teardown.
- **Owned module integration:** separately linked generic x86-64 ELF client/provider inputs, including an actual linker-generated `R_X86_64_JUMP_SLOT`; exact declared provider resolution; checked relocation/write/readback; supervised native transfer to a known synthetic exit; identical sealed source inputs across worker launches; negative tests for mismatch, invalid mapping, stale provider generation and mapping-owner epoch. This is a **test-only research path**, not a general Sony runtime loader or PS5 entry ABI.
- **Owned PS5-format first instruction:** the source-built SHA-256-pinned minimal title (entry `0x10`, first opcode `UD2`) has eight linker/converter-produced `R_X86_64_RELATIVE` records. The Linux-only opt-in sealed research worker parses the actual image, applies all eight patches with prepared-memory readback, and reaches the expected native illegal-instruction fault in **two** launches; altered bytes are refused. Only a demonstrably empty PT_TLS descriptor is omitted in the transient synthetic profile. This is **not** an ordinary PS5 startup contract or commercial guest execution. [Pinned CI evidence](https://github.com/astraea-emu/astraea/actions/runs/38075447544).
- **Public homebrew static analysis (non-executable):** PR #404 pinned an independent PS5-marked `ET_DYN` Breakout ELF; #406 merged the explicit read-only `--ps5-raw-elf` analyzer. The exact unmodified external executable SHA-256 is `05c414993d1cd9e7182bbf5647be1ffad72c6ed6b6f5399d01af27281fef48b8`. Merged #408's independently tested, bounded GNU-hash parser yields 5 needed libraries, 20 dynamic symbols, 1 `RELATIVE` and 19 `GLOB_DAT` entries. Draft #409 independently checks 19/19 NIDs against the original author's frozen source build recipe; its test does not establish Sony service behavior. **None are executed or resolved to working runtime providers.** Production `diagnose` still refuses this raw ELF with `loader_rejected` before entry.
- **Graphics:** V3 proves a limited submitted PM4/DCB-to-Vulkan raster route. It does **not** establish general shader ISA, descriptors, resource lifetime/coherence, surface tiling/compression, synchronization or VideoOut.
- **Verification:** five standard CI jobs (Linux x64, Windows x64, macOS ARM64, Linux ASan/UBSan, Linux Clang fuzz smoke) plus a pinned public-title source-build workflow on relevant changes. CTest counts vary by platform; passing tests do not imply game compatibility.

## Pinned public evidence and first blocker

The independent, public, source-built native title is [BlackBear's pinned boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate/tree/2f672d1c2f508e26f82ce6e27cef289a0861413c) with an Astraea-owned pre-CRT observer. Astraea's production CLI reports 14 program headers, five PT_LOAD segments, four generic needed entries, four SCE needed-module entries, four SCE import libraries, 26 dynamic symbols and 40 RELA records. `astraea diagnose` stops **before entry** at `unsupported_dynamic_dependencies`, not at a successful game launch.

A second source-built, non-Sony `libc.prx` has 2,669 dynamic symbols and 1,896 relocation records. The reproducible *read-only* cross-artifact study found **25 relocation-referenced title imports**, with **0 full literal NID/library/module matches**, **8 NID-only lexical matches** and **17 with no equal NID** in the source-built PRX. No provider identity, runtime service, guest call or relocation binding follows from these counts.

The pinned compiled CRT object separately contains one relocation from the authored CRT entry function to undefined `_init_env` (raw x86-64 relocation type 41 at object-section offset `0x15`). This establishes a **compiled-object dependency**, not a resolved final SCE-format import, a provider, an executed call or a Sony ABI fact. See [measured public pair evidence](research/public_pinned_title_prx_lexical_closure_2026-10-09.md), [native-title baseline](research/public_native_title_closure_2026-10-08.md) and [the second PRX inventory](research/public_cleanroom_prx_second_corpus_2026-10-08.md).

**Latest smaller source-built PS5-format diagnostic (PR #400):** a separate independent minimal homebrew image contains no needed modules/import libraries or undefined named imports, and has **eight** `R_X86_64_RELATIVE` RELA records. Its first production `diagnose` boundary is `unsupported_relocations` (`pre_entry`), with **zero** guest instructions executed. This supersedes the larger public boilerplate as the *first selected PC-only integration workload*, not its retained provenance and negative findings. [The exact merge-head CI](https://github.com/astraea-emu/astraea/actions/runs/38069624888) passed all five standard jobs.

## Next executable proof — C1 software-first

1. **Source-built PS5-format first-instruction research proof complete.** PRs #402/#403 establish eight checked `RELATIVE` writes and two supervised `UD2` faults for the exact pinned homebrew image; production `diagnose` still reports `unsupported_relocations` before entry. No retail C1 claim.
2. **Advance from verified static identity to a first bounded external-service boundary.** #404/#406/#408 are merged with pinned source/ELF hashes, five libraries, 20 symbols and 20 relocations. Draft #409 checks all 19 imported NIDs against author-declared names and module groupings; this remains static-source provenance, not runtime linking. Once that independent import oracle passes protected CI, plan a first actual service request in a separately isolated test-only worker; do not infer providers from bare NIDs, invent Sony return values, or admit arbitrary native guest bytes.
3. **Preserve the larger public-title closure findings.** Its `_init_env`/PRX identity and runtime-contract limits remain unresolved. Do not treat the shared return-zero stub as a functional provider. Use [#378](https://github.com/astraea-emu/astraea/issues/378) to track the integrated gate rather than adding disconnected import helpers.
4. **Select a lawfully held low-closure retail executable** for local, read-only profiling once available. Keep game bytes, keys, system modules and firmware outside the repository.
5. **Use reference hardware only for irreducible entry-state evidence.** [#334](https://github.com/astraea-emu/astraea/issues/334) remains open until two identical normal-title entry observations on an authorized PS5 exist; a payload-loader entry is not equivalent. Promote only evidenced selected-path fields via [#348](https://github.com/astraea-emu/astraea/issues/348). First real post-entry divergence is [#350](https://github.com/astraea-emu/astraea/issues/350).

The [C1 PC-first execution and hardware readiness note](research/c1_pc_first_execution_hardware_readiness_2026-10-09.md) sets the console stop rules. **No PS5 hardware action is required now.**

## Separate, nonblocking work

- **Graphics evidence:** [#191](https://github.com/astraea-emu/astraea/issues/191) and [#207](https://github.com/astraea-emu/astraea/issues/207) require observed AGC LinkShaders effects before success semantics are promoted.
- **Native guest admission remains a security gate:** current Linux syscall trapping is guest-instruction-pointer-range based; do not treat this as a proven hostile-code sandbox or admit arbitrary third-party native bytes. Evaluate guest control-flow escapes and trusted syscall brokerage before broadening test-only source-pinned execution (tracked in [#298](https://github.com/astraea-emu/astraea/issues/298)).
- **Host containment:** [#320](https://github.com/astraea-emu/astraea/issues/320) keeps arbitrary Windows retail entry disabled until equivalent raw guest-syscall interception is proven; [#298](https://github.com/astraea-emu/astraea/issues/298) covers optional Linux Landlock hardening.
- **Host ISA:** [#319](https://github.com/astraea-emu/astraea/issues/319) activates when a measured selected workload encounters unsupported x86 instructions.
- **Repository administration:** [#168](https://github.com/astraea-emu/astraea/issues/168) tracks protection, merged-branch cleanup, automatic branch deletion and metadata. These actions do not change emulator semantics.

## Nonclaims and source of truth

Astraea is not currently a playable PS5 emulator. It does not claim complete userland/kernel services, PS5 initial-process state, hostile-code sandboxing on every host, production GPU coverage or better performance/compatibility than existing emulators. The exact compilation and test conditions matter; a green CI check means only its specified assertions passed.

For details, read [the governing architecture and gates](PROJECT_PLAN.md), [accepted ADRs](adr/README.md), [clean-room policy](CLEAN_ROOM.md) and [current research index](research/README.md). Merged source and tests determine implemented behavior; this file states the active priority; dated research records retain historical observations.
