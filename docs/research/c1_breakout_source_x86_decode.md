# C1: source-backed x86-64 decoding at the real Breakout callee

**State:** research-only decode experiment; PR #433 (unmerged at authorship). The CI output, not this note, determines observed machine bytes.

## Problem and chosen boundary

The pinned independently authored SharpEmu Breakout ELF has source-backed entry RIP `0x7a0`; the deliberately narrow #432 interpreter modeled `AND RSP, -16` and `CALL rel32`, with target `0x210`. It does not execute the callee. Expanding CPU semantics by writing title-specific byte recognizers is not a sustainable architecture.

## Decoder choice and licensing

Research only: Zydis **v4.1.1** pinned to commit `a2278f1d254e492f6a6b39f6cb5d1f5d515659dc` and Zycore **v1.5.2** pinned to `0b2432ced0884fd152b471d97ecf0258ff4d859f`. Both upstream projects use the MIT license, compatible with Astraea's GPLv3 distribution model when their notices are retained. No decoder source is copied into this repository. Decoder/encoder formatting tools and third-party tests are disabled in the opt-in target.

Zydis is a maintained, architecture-aware x86-64 **decoder**, not a semantic interpreter, sandbox, translation engine, GPU backend, Sony service implementation or guarantee that an opcode is safe to execute. Only `ASTRAEA_BUILD_C1_RESEARCH=ON` on Linux x86-64 builds this experimental integration; `astraea_core` and production `astraea` do not link it.

## Evidence contract

`decode_source_x86_64` accepts an existing `GuestImage`, a guest RIP and a finite instruction budget (1–12). It checks the source executable mapping and reads at most the architectural 15-byte x86 instruction limit per decode through `initialized_image_view`; it returns instruction RIP, byte length, original bytes and typed mnemonic ID. It refuses malformed or truncated encodings, unavailable source bytes and outside-executable source addresses.

The pinned Breakout workflow independently compares every emitted raw callee instruction with its byte extent in the original exact-SHA ELF and checks contiguous instruction boundaries. It also checks that the first two entry decodes independently agree with the existing #432 interpreter. This is decode provenance, not execution or semantic correctness.

## Next dependency

Review the actual 12-instruction `0x210` callee prefix and group architectural semantics by general operation (register, stack, memory read/write, branch, fault), rather than by game title. Design a typed execution core that enforces owned guest memory, status flags, call/return and rigorous stop semantics. Only then implement the smallest necessary subset and progressively approach the first *actually executed* imported-service boundary.

**Retained blockers:** #410 guest-host write containment for native execution; deliberately non-faithful native Breakout mapping and unknown bare-NID provider identities (#378). No imported-service transfer, graphics or gameplay is inferred from decoded instructions.
