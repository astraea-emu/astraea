# C1 second source-owned PS5-format entry — build and native trace checkpoint

**Status:** source build accepted in merged #447; bounded native execution accepted in merged #448. Independent homebrew and arbitrary native guest execution **not established**.  
**Date:** 2026-10-12  
**Scope:** a second Astraea-authored fixture, **not** independent homebrew, retail, a process-ABI observation, a Sony module or a game.

## Motivation

The exact native C1 `GuestImage` reference fixture (`45920de847b2d33896c186654bbf22a728f1910fad84accb15204aa2da50c678`) begins with `UD2`. External ptrace, post-bootstrap seccomp, Linux kernel VMA audit and all eight checked `R_X86_64_RELATIVE` patches have been verified on this original fixture. The absence of a preceding ordinary CPU instruction means its native execution result cannot establish multi-instruction guest progression.

A separate generic Astraea-owned x86 assembly probe already shows `mov eax, 42; UD2`, but it is not an independently converted PS5-format `GuestImage`. The next meaningful source-owned CPU validation is a **separately built PS5-format ELF containing those exact two instructions** followed by a real typed fault.

## Frozen construction candidate

- Upstream public **build toolchain**: `blackbearreloaded/ps5-native-app-boilerplate@2f672d1c2f508e26f82ce6e27cef289a0861413c`.
- Existing Astraea preparation: `tools/reference/ps5_process_entry_observer/prepare_minimal_public_title.py` with `--entry-mode mov42-ud2` in a separate, clean detached worktree; original default `--entry-mode ud2` unchanged.
- Exact independently authored C++ naked `_start` assembler string: `movl $42, %eax` then `ud2`. This changes no other source-authored runtime API, initial process ABI, requested imports or ELF converter semantics.
- Expected x86-64 bytes at `e_entry=0x10`: `b8 2a 00 00 00 0f 0b`.
- Compiler-produced PS5/SCE ELF64 executable, `e_type=0xfe10`, `e_machine=62`, zero declared needed/import-library records; the eight `RELATIVE` relocations are source-converter-generated process data.
- Candidate ELF file size: **66,600 bytes**.
- Candidate ELF SHA-256: `cdeab7a242aa88a90a01494ff85ac68f44b15e7a2e588498927e3aa6aae5a947`.

The candidate digest appeared in two distinct GitHub Actions source builds (first build `38287259418`, despite a subsequently corrected Python escaped-literal verifier, and second passing build `38287635928`). **Merged #447 and #448 exact-head CI reproduced the hash, entry bytes and relocation/import conditions and passed all seven required checks.** No binary or copyrighted public dependency is committed to Astraea.

## Measured execution gate — complete in #448

The second ELF was admitted only through its separate pinned SHA-256 research profile, not through production `diagnose`, the original pinned `UD2` profile, or a general outside-authored ELF loader. Using the existing `GuestImage`/checked relocation path, an independently supervising Linux ptrace parent verified kernel page permissions, eight relative relocation results and source-backed entry bytes. In each of two reproducible executions:

1. An explicitly synthetic non-Sony context entered the original two-instruction code.
2. The source-built `mov eax,42` retired; the parent observed `RAX=42` and `RIP=entry+5`.
3. The original following `UD2` raised real `SIGILL` at `entry+5`; no additional ordinary guest instruction retired.
4. Tampered ELF bytes were refused before entry, a direct unbound leaf did not succeed, and the original and new pinned admission modes rejected each other's artifacts.

The pinned observer, independent Breakout static closure and five standard CI jobs all passed on exact head `79e9ce984022dea8d2bd326584feb6a24c33f47b`. [PR #448](https://github.com/astraea-emu/astraea/pull/448) was squash-merged as `9c38ab75d6a65f0bef3d2a41e50494894b3ab849`.

**Boundaries:** this is source-owned, exact-hash, Linux x86-64 native research, **not** native Breakout or retail PS5 execution; no genuine Sony entry ABI, arbitrary guest containment or title-generated frame was established. The next measured work belongs to #410 native containment and #378 Breakout's guest/host mixed W/X memory mapping plus unresolved imported service, not another isolated instruction-count proof.
