# C1 owned CPU instruction-step contract

Status: opt-in research only. These semantics neither admit arbitrary native
guest instructions nor assert a real Sony process-entry ABI.

## Separation of responsibilities

- `source_x86_64_decoder` reads original executable guest bytes through
  `GuestImage` and returns a bounded source-backed instruction record.
- `step_owned_x86_64` re-decodes one supplied record with pinned Zydis,
  verifies current guest RIP, and handles a small generic subset:
  `PUSH r64`, `MOV r64,r64`, `SUB r64,imm`, and
  `MOV qword [base+disp],imm`.
- `OwnedGuestAddressSpace` validates read/write/fetch permissions on
  caller-owned buffers and refuses unmapped or cross-boundary accesses.
- `breakout_owned_callee_execution` performs title-specific import
  provenance checks **after** a typed pre-retirement control-flow stop.
  It cannot bind a guessed provider or synthesize a service return value.

On a supported instruction, register state and RIP commit only after operand
validation and any single memory write succeed. A denied write, decode mismatch,
RIP mismatch, unknown instruction or control transfer leaves CPU state intact.
The CPU executor does not claim multi-instruction transactional rollback.
The research memory model cannot prevent external host aliases or protect
against arbitrary native machine code in the same host address space.

## Acceptance and limits

An independent source-owned fixture exercises general register, flag, stack,
memory and rejection semantics without a Breakout ELF. The pinned Breakout
workflow must independently retain 11 callee retired instructions, nine guest
stack writes and an unresolved pre-CALL stop at RIP `0x23c` / GOT `0x68c8`
/ bare NID `Vo5V8KAwCmk`; the imported CALL itself does not retire.

This is **not** a complete x86-64 interpreter, JIT, dynamic linker, MMU,
thread scheduler, secure host sandbox, or demonstration of gameplay. A
general-purpose CPU backend must also mediate instruction fetch, generic loads,
branch/return semantics, faults, TLS and guest virtual memory permission changes,
with a much larger owned conformance corpus and independent oracles.
