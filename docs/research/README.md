# Research Notes

This directory stores durable evidence for platform behavior and unresolved questions.

A useful research note contains:

- exact question
- public/controlled sources
- observations
- competing explanations
- confidence level
- implementation implications
- tests that should encode the conclusion
- date of last verification

Do not store proprietary dumps, keys, firmware, SDK material, or copyrighted retail content here.

## Interpreting historical notes

Research notes are evidence records, not the live project scheduler. A dated or
issue-scoped note may intentionally preserve wording such as "current",
"next", or "remaining" from the point at which that evidence was collected.
Do not reinterpret those time-local statements as today's implementation
frontier.

For the live merged frontier and next critical dependency, read
`docs/STATUS.md` and then inspect open GitHub issues/PRs. When a research
conclusion itself is superseded by new evidence, annotate or supersede the note;
do not rewrite historical observations merely to make their prose sound current.

## Evidence maps

- [PS5 executable/module ABI](ps5-executable-module-abi.md) — public evidence for SCE ELF/module metadata, identity, NIDs, and unresolved ABI questions.
- [RDNA2 / PS5 graphics](rdna2-ps5-graphics.md) — official/vendor facts, public graphics observations, IR boundaries, PS5-specific unknowns, and falsification experiments.

## Active architecture / compatibility research

- [PS5 initial-process ABI](ps5_initial_process_abi.md) — C1 evidence for retail entry registers, parameter block, TLS/TCB, and bootstrap ordering.
- [PS5 process-entry reference observation](ps5_process_entry_probe.md) — transport-neutral C1 hardware-observation contract, structural normalization, and repeat-run comparison.
- [PS5 procparam runtime observation](ps5_procparam_observation.md) — C1B selected-artifact relationship test for `sceKernelGetProcParam()` vs mapped `PT_SCE_PROCPARAM`.
- [Public Breakout static closure (2026-10-10)](https://github.com/astraea-emu/astraea/pull/404) — immutable independent archive and ELF SHA-256, original production `loader_rejected` result. [Opt-in raw-ELF analysis](https://github.com/astraea-emu/astraea/pull/406) remains separate from execution; [bounded GNU-hash inventory](https://github.com/astraea-emu/astraea/pull/408) is merged, and [author-import NID provenance](https://github.com/astraea-emu/astraea/pull/409) is merged. None establishes a retail game boot.
- [First source-built PS5-format instruction checkpoint (2026-10-10)](https://github.com/astraea-emu/astraea/pull/403) — opt-in sealed Linux worker maps a SHA-256-pinned source-owned SCE ELF, applies eight RELATIVE relocations, and repeats its authored `UD2` fault twice; synthetic entry only, no commercial title or Sony ABI proof. [Exact pinned CI](https://github.com/astraea-emu/astraea/actions/runs/38075447544).
- [C1 execution checkpoint (2026-10-10)](https://github.com/astraea-emu/astraea/pull/400) — pinned source-built import-free minimal PS5-format ELF, eight `RELATIVE` relocations and `unsupported_relocations` pre-entry stop; [generic two-ELF JUMP_SLOT execution proof](https://github.com/astraea-emu/astraea/pull/399) remains research-only. Follow [#378](https://github.com/astraea-emu/astraea/issues/378) for the live executable integration gate.
- [C1 PC-first execution and reference-console readiness (2026-10-09)](c1_pc_first_execution_hardware_readiness_2026-10-09.md) — priority gates, independent corpora, true milestone blockers and the conditional firmware-13.00 hardware plan.
- [Independent native-title closure baseline (2026-10-08)](public_native_title_closure_2026-10-08.md) — bounded public-source ELF profile, supervised pre-entry dependency stop, and CI reproducibility; not real PS5 hardware evidence.
- [Dependency manifest v0](dependency_manifest_v0.md) — checked exact raw dependency, SCE long-form symbol and relocation census, with opt-in public-field decoding limits.
- [Public SCE packed identity experiment](public_sce_packing_v1.md) — source-attributed local module/library IDs and their evidence boundaries.
- [Second clean-room PRX corpus](public_cleanroom_prx_second_corpus_2026-10-08.md) — independent source-generated non-Sony module with 2,669 symbols, 1,896 relocations and TLS metadata.
- [Owned module graph](module_graph_owned_contract_2026-10-08.md) — bounded explicitly declared provider/relocation/write semantics, not a verified PS5 runtime linker.
- [Post-C0 architecture review — 2026-09-24](architecture_review_2026-09-24.md) — pinned comparison with public emulator/toolchain/compiler projects and resulting roadmap decisions.
- [Retail execution supervisor](retail_execution_supervisor.md) — C0 threat model and supervisor design; Linux production diagnostic gate is now implemented, while Windows retail admission remains disabled.
