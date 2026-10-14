# C1 original Breakout — bounded mediated batch verification

**Status:** research-only candidate, subject to exact-head CI qualification.
This is not a playable PS5 emulator, Sony ABI emulation, native guest
execution, or proof of a production CPU engine.

## Input provenance and exact boundary

- The original independent SharpEmu Breakout PS5-marked ELF is unchanged
  (SHA-256 `05c414993d1cd9e7182bbf5647be1ffad72c6ed6b6f5399d01af27281fef48b8`).
- Astraea produces its independently checked 13-retirement reference using
  the existing virtual guest-memory model, preserved RELATIVE relocation,
  19 unresolved GLOB_DAT slots and explicitly **synthetic** entry context.
- The research script `compare_breakout_unicorn_batch.py` reuses the
  version-pinned Unicorn 2.1.4 wheel, original ELF loader and trace parser
  from `compare_breakout_unicorn.py`. It creates fresh isolated mediated
  Unicorn virtual memory for each case.
- The immutable imported-call boundary is guest RIP `0x23c`, with the
  first unresolved GOT word at `0x68c8`. The import is **not retired**.

## Independent batch comparison

The existing stepwise comparison requires thirteen individual, exact-state
mediated calls. The new experiment uses **one bounded `emu_start` call
with `count=13`** in each of two fresh research engine instances:

1. An unhooked-*code*-trace batch (memory hooks still observe writes),
   with final CPU state and memory verified after stopping.
2. A code-hook-guarded batch that independently checks the exact 13
   pre-instruction RIPs, and stops/refuses unreviewed control flow.

For both cases, the complete final GPR/RFLAGS/FS/GS CPU context matches the
checked Astraea reference (architecturally undefined AF masked only as
needed), the observed ten total entry-plus-callee guest stack-store addresses/widths/values match
the reference, and the **entire final 4-KiB synthetic stack** matches
the ordered-write replay. The real RELATIVE patch and original unresolved
first GOT word remain identical. The bounded instruction count and terminal
RIP prevent a fourteenth imported-service instruction from being admitted.

The code hook is **pre-execution instrumentation**, not proof of retirement
or a malicious-code sandbox. A memory-write callback alone does not prove
committed bytes: the check also compares final memory against the expected
ordered write sequence. This is valid only for the selected source-backed
subset; it does not cover restartable partial faults or arbitrary opcodes.

## Negative controls

- Reject an altered final reference CPU register with intact source bytes.
- Reject a modified source ELF by exact SHA-256 before executing it.
- Reject a twelve-instruction requested budget instead of accepting a
  truncated result as equivalent to the reviewed thirteen-step boundary.

This extends the already verified source-owned performance finding from
PR #467 to the **actual original Breakout instruction sequence**, without
assigning any Sony service provider or guessing a return value.

## Next useful milestone

If the batch proof passes, prioritize verifying the original required
import's provider/module identity and actual side effects; eventually an
authenticated test-only runtime-service model could allow legitimate
homebrew progress past the import. A guessed unconditional return-zero
function is not acceptable. Keep native containment #410 independent.
The CPU-backend decision in ADR 0013 still requires wider ISA, ABI,
fault, memory isolation, licensing and representative performance evidence.
