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

## Evidence maps

- [PS5 executable/module ABI](ps5-executable-module-abi.md) — public evidence for SCE ELF/module metadata, identity, NIDs, and unresolved ABI questions.
- [RDNA2 / PS5 graphics](rdna2-ps5-graphics.md) — official/vendor facts, public graphics observations, IR boundaries, PS5-specific unknowns, and falsification experiments.

## Active architecture / compatibility research

- [PS5 initial-process ABI](ps5_initial_process_abi.md) — C1 evidence for retail entry registers, parameter block, TLS/TCB, and bootstrap ordering.
- [Post-C0 architecture review — 2026-09-24](architecture_review_2026-09-24.md) — pinned comparison with public emulator/toolchain/compiler projects and resulting roadmap decisions.
- [Retail execution supervisor](retail_execution_supervisor.md) — C0 threat model and supervisor design; Linux production diagnostic gate is now implemented, while Windows retail admission remains disabled.
