# C1 owned CPU instruction-step contract

Status: opt-in research only. These semantics neither admit arbitrary native
guest instructions nor assert a real Sony process-entry ABI.

## Separation of responsibilities

- `source_x86_64_decoder` reads original executable guest bytes through
  `GuestImage` and returns a bounded source-backed instruction record.
- `step_owned_x86_64` re-decodes one supplied record with pinned Zydis,
  verifies current guest RIP, and handles a small generic subset:
  `PUSH r64`, `MOV r64,r64`, `SUB r64,imm`, `AND r64,imm`,
  `MOV qword [base+disp],imm` and a checked direct `CALL rel32`.
- `OwnedGuestAddressSpace` validates read/write/fetch permissions on
  caller-owned buffers and refuses unmapped or cross-boundary accesses.
- `run_owned_x86_64` fetches current bytes through that virtual memory,
  decodes and steps under a strict 4,096-instruction maximum; stops retain
  guest RIP, typed fault or unsupported step, and already committed work.
- `observe_owned_indirect_call` validates the current guest-executable
  bytes of a stopped `CALL qword ptr [RIP+disp]`, computes the RIP-relative
  operand address and reads the eight-byte word under guest read permission.
  The returned raw word is **opaque**: the API does not follow it, validate
  executable ownership of its value, assign a Sony provider, change CPU
  registers/stack or retire the CALL. Independent owned tests refuse
  unexecutable instructions, altered bytes, non-RIP-relative calls,
  inaccessible/truncated operands and mismatched CPU state.
- The pinned Breakout integration stages each original `GuestImage` mapping
  fragment into disjoint caller-owned buffers, keeping source virtual
  permissions (including **virtual** W+X) and a separate synthetic RW stack.
  The bounded research address-space table has eight mapping slots, enough
  for the four pinned source fragments plus stack. No host executable
  mapping, native instruction entry, provider resolution or actual Sony ABI
  startup follows. The original entry CALL remains a separately verified
  synthetic-context model.
- `breakout_owned_callee_execution` performs title-specific import
  provenance checks **after** a typed pre-retirement control-flow stop.
  It cannot bind a guessed provider or synthesize a service return value.

On a supported instruction, register state and RIP commit only after operand
validation and any single memory write succeed. A denied write, decode mismatch,
RIP mismatch, unknown instruction or control transfer leaves CPU state intact.
The CPU executor does not claim multi-instruction transactional rollback.
The generic direct CALL model preflights the destination's virtual execute
permission and return-address stack write; only then can it commit guest RIP,
RSP and one owned return word. Indirect CALL/unknown control flow remain
unretired. For AND, CF/OF clear, PF/SF/ZF update, and architecturally
undefined AF remains outside the claimed flag mask.

The exact Breakout entry plus callee can now be fetched/interpreted in one
bounded generic CPU run: two genuine source-backed entry instructions and
eleven callee instructions, terminating at the same unretired import CALL.
The independently authored two-instruction interpreter remains a comparison
oracle, not an alternative Sony ABI. The synthetic entry context is unchanged.
The research memory model cannot prevent external host aliases or protect
against arbitrary native machine code in the same host address space.

## Acceptance and limits

An independent source-owned fixture exercises general register, flag, stack,
virtual instruction fetch, finite budgets, source mutation, memory and refusal
semantics without a Breakout ELF. The pinned Breakout
workflow must independently retain 11 callee retired instructions, nine guest
stack writes and an unresolved pre-CALL stop at RIP `0x23c` / GOT `0x68c8`
/ bare NID `Vo5V8KAwCmk`; the imported CALL itself does not retire.

This is **not** a complete x86-64 interpreter, JIT, dynamic linker, MMU,
thread scheduler, secure host sandbox, or demonstration of gameplay. A
general-purpose CPU backend must also mediate instruction fetch, generic loads,
branch/return semantics, faults, TLS and guest virtual memory permission changes,
with a much larger owned conformance corpus and independent oracles.
