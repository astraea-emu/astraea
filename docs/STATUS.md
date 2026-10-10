# Project Status

**As of 2026-10-09:** C0 Linux retail diagnostics and bounded V0–V3 graphics are merged. C1 is active. **No commercial PS5 game has entered its first guest instruction, booted, rendered a title-generated frame or reached gameplay in Astraea.**

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
- **Owned module integration:** separately parsed source-authored client/provider ELF inputs; exact declared provider resolution; checked relocation/write/readback; supervised native transfer to a known synthetic exit; identical sealed source inputs across worker launches; negative tests for mismatch, invalid mapping, stale provider generation and mapping-owner epoch. This is a **test-only research path**, not a general Sony runtime loader or PS5 entry ABI.
- **Graphics:** V3 proves a limited submitted PM4/DCB-to-Vulkan raster route. It does **not** establish general shader ISA, descriptors, resource lifetime/coherence, surface tiling/compression, synchronization or VideoOut.
- **Verification:** five standard CI jobs (Linux x64, Windows x64, macOS ARM64, Linux ASan/UBSan, Linux Clang fuzz smoke) plus a pinned public-title source-build workflow on relevant changes. CTest counts vary by platform; passing tests do not imply game compatibility.

## Pinned public evidence and first blocker

The independent, public, source-built native title is [BlackBear's pinned boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate/tree/2f672d1c2f508e26f82ce6e27cef289a0861413c) with an Astraea-owned pre-CRT observer. Astraea's production CLI reports 14 program headers, five PT_LOAD segments, four generic needed entries, four SCE needed-module entries, four SCE import libraries, 26 dynamic symbols and 40 RELA records. `astraea diagnose` stops **before entry** at `unsupported_dynamic_dependencies`, not at a successful game launch.

A second source-built, non-Sony `libc.prx` has 2,669 dynamic symbols and 1,896 relocation records. The reproducible *read-only* cross-artifact study found **25 relocation-referenced title imports**, with **0 full literal NID/library/module matches**, **8 NID-only lexical matches** and **17 with no equal NID** in the source-built PRX. No provider identity, runtime service, guest call or relocation binding follows from these counts.

The pinned compiled CRT object separately contains one relocation from the authored CRT entry function to undefined `_init_env` (raw x86-64 relocation type 41 at object-section offset `0x15`). This establishes a **compiled-object dependency**, not a resolved final SCE-format import, a provider, an executed call or a Sony ABI fact. See [measured public pair evidence](research/public_pinned_title_prx_lexical_closure_2026-10-09.md), [native-title baseline](research/public_native_title_closure_2026-10-08.md) and [the second PRX inventory](research/public_cleanroom_prx_second_corpus_2026-10-08.md).

## Next executable proof — C1 software-first

1. **Trace the observed startup dependency** through the *pinned intermediate linked ELF* and independently source-built final SCE ELF. Check that the same undefined `_init_env` dynamic symbol maps to one exact final SCE symbol identity, with complete toolchain provenance. Preserve the identity as **artifact-local**; do not infer Sony provider authority from equal NID text.
2. **Determine whether an independently authored provider is actually evidenced.** If missing, report the exact unsupported import/module; no fabricated binding, guessed TLS module index or blanket HLE success response.
3. **Advance issue [#378](https://github.com/astraea-emu/astraea/issues/378) with an executable workload.** Reuse the existing owned two-ELF supervisor, mapping ownership and relocation path. Require byte-identical pinned inputs, repeatable terminal events and negative/no-mutation cases. Do not add another general-purpose loader unless the first measured dependency requires it.
4. **Select a lawfully held low-closure retail executable** for local, read-only profiling once available. Keep game bytes, keys, system modules and firmware outside the repository.
5. **Use reference hardware only for irreducible entry-state evidence.** [#334](https://github.com/astraea-emu/astraea/issues/334) remains open until two identical normal-title entry observations on an authorized PS5 exist; a payload-loader entry is not equivalent. Promote only evidenced selected-path fields via [#348](https://github.com/astraea-emu/astraea/issues/348). First real post-entry divergence is [#350](https://github.com/astraea-emu/astraea/issues/350).

The [C1 PC-first execution and hardware readiness note](research/c1_pc_first_execution_hardware_readiness_2026-10-09.md) sets the console stop rules. **No PS5 hardware action is required now.**

## Separate, nonblocking work

- **Graphics evidence:** [#191](https://github.com/astraea-emu/astraea/issues/191) and [#207](https://github.com/astraea-emu/astraea/issues/207) require observed AGC LinkShaders effects before success semantics are promoted.
- **Host containment:** [#320](https://github.com/astraea-emu/astraea/issues/320) keeps arbitrary Windows retail entry disabled until equivalent raw guest-syscall interception is proven; [#298](https://github.com/astraea-emu/astraea/issues/298) covers optional Linux Landlock hardening.
- **Host ISA:** [#319](https://github.com/astraea-emu/astraea/issues/319) activates when a measured selected workload encounters unsupported x86 instructions.
- **Repository administration:** [#168](https://github.com/astraea-emu/astraea/issues/168) tracks protection, merged-branch cleanup, automatic branch deletion and metadata. These actions do not change emulator semantics.

## Nonclaims and source of truth

Astraea is not currently a playable PS5 emulator. It does not claim complete userland/kernel services, PS5 initial-process state, hostile-code sandboxing on every host, production GPU coverage or better performance/compatibility than existing emulators. The exact compilation and test conditions matter; a green CI check means only its specified assertions passed.

For details, read [the governing architecture and gates](PROJECT_PLAN.md), [accepted ADRs](adr/README.md), [clean-room policy](CLEAN_ROOM.md) and [current research index](research/README.md). Merged source and tests determine implemented behavior; this file states the active priority; dated research records retain historical observations.
