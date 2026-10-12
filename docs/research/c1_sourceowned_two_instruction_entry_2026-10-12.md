# C1 second source-owned PS5-format entry — build and identity checkpoint

**Status:** build-only research candidate; native execution **not attempted**  
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

The candidate digest appeared in two distinct GitHub Actions source builds (first build `38287259418`, despite a subsequently corrected Python escaped-literal verifier, and second passing build `38287635928`). **The PR final-head CI must reproduce the same hash, bytes and import/relocation conditions before this candidate becomes accepted evidence.** No binary or copyrighted public dependency is committed to Astraea.

## Next measured execution gate (not part of the build PR)

1. Separate hash-specific admission for this **one reviewed fixture** under the existing externally supervising ptrace research setup; do not silently broaden production `diagnose` or the original pinned UD2 experiment.
2. Independently validate the entire source identity, ELF mapping plan, relocation target/readback, executable page permissions, blocked host syscalls and absence of controller side effects.
3. Under a synthetic, explicitly non-Sony context, retire exactly the one guest `mov eax,42` instruction. The independent tracer must observe `RAX=42`, `RIP=entry+5`, followed by real `SIGILL` on the original `UD2` at `entry+5` with no further instruction retirement.
4. Reproduce twice from identical bytes and require pre-entry tampered source, mismatched branch/mapping, and unsupported identity negatives. Retain finite budgets and external termination.
5. Preserve all seven exact-head CI checks and do not infer compatibility with any independent Breakout ELF or commercial PS5 title.

**Unsupported remains unsupported:** #410 arbitrary native guest containment, Breakout mixed W/X page and ambiguous imported Sony service #378, and authentic commercial process-entry ABI #334/#348.
