# C1 research CPU effect ledger — bounded contract

**Status:** opt-in research only; not a production CPU adapter, host sandbox,
guest scheduler, Sony ABI, or complete x86 instruction trace.

## Why the old single-store trace is deliberately unchanged

The current checked interpreter supports a narrow instruction subset with at
most one eight-byte committed guest write and clean pre-retirement failures.
Its existing `OwnedCpuTraceStep` remains correct **within that subset**.
Merged PR #464 demonstrated, independently under a hash-pinned Unicorn 2.1.4
research engine, that `REP MOVSB` may commit one iteration and then fault
on the next while keeping RIP at the same instruction with restartable
RCX/RSI/RDI. A universal "fault = no effects" contract would be wrong.

## New research-only representation

`owned_cpu_effect_ledger.hpp` offers one single-use ledger per instruction:

- `begin(before_context)`: freeze original guest RIP/full GPR, RFLAGS,
  FS/GS base; the caller owns a real `GuestCpuContext`.
- `append_completed_write(address, bytes, owned_virtual_memory)`: accept
  only an **already performed, explicitly producer-attested** guest write;
  verify the exact supplied byte values using virtual guest readback.
- `finish(retired, after_context, none)`: record zero or more verified
  writes and a complete terminal CPU state.
- `finish(refused_before_effects, unchanged_context, typed_fault)`:
  only possible with zero writes and identical before/after context.
- `finish(fault_after_effects, restartable_context, typed_fault)`:
  requires at least one completed write and still-faulting RIP.

Each write records guest VA, explicit width, and exact bytes up to **64
bytes**. Up to **64 write events per instruction** are permitted; exceeding
either limit, overflowing guest VA, finding unreadable/unmapped bytes, or
observing contradictory terminal state permanently invalidates that ledger.
There is no silently truncated successful report, no heap allocation inside
the ledger, and no native host pointer in any event.

This API **does not determine what actually committed**: even successful
readback cannot prove causal execution if old and new bytes are identical.
A generic Unicorn `UC_HOOK_MEM_WRITE` callback is not a post-commit
guarantee. Neither concurrent guest remaps, overlapping historical writes,
nor atomicity of locked instructions is established. A real backend adapter
must supply independently justified committed-effect provenance and define
the coherence/memory-lifetime boundary; otherwise it must refuse, not guess.

## Acceptance and nonclaims

`astraea_c1_owned_effect_ledger_research` is a Linux x86-64, opt-in,
source-owned C++ test target run by the pinned C1 public Breakout CI gate.
Its tests:

1. Step the **existing checked Astraea `PUSH RBP`** through the real
   virtual guest-memory model and attest the produced eight-byte store.
2. Show a real denied `PUSH` into read-only virtual memory produces an
   unchanged context and zero ledger write events.
3. Record synthetic source-owned 16-byte and two-byte guest writes with
   exact virtual-memory readback.
4. Represent the previously independently observed restartable `REP`
   fault state **as an explicit source-owned model**, not as an execution of
   REP by the existing Astraea interpreter.
5. Refuse uncommitted hook-like guesses, full-ledger overflow, missing read
   permission, unsupported sizes, address overflow and invalid transitions.

This is a **shape and invariant proof** for comparing future backends,
not a demonstration that Astraea implements vector stores, REP string
instructions, or LOCK XADD. The independent mediated Unicorn tests of
PR #464 remain the external semantic evidence. No new dependency enters
production builds. No outside-authored guest is admitted to native execution.

## Next integration gate

Select one mediated candidate and build a sealed source-owned adapter that
reports committed effects with defensible provenance and compares complete
CPU state, write order, fault stage and restartable progress. Add repeated
writes crossing mappings, read-modify-write, vector widths, overlapping
writes, fault recovery and budget handling, while carefully separating
observation from authorization. Then measure controlled performance and
evaluate version-specific license/host support before deciding ADR 0013.

## First independently executed mediated-engine bridge

The research-only `emit_unicorn_owned_effects.py` program executes fixed,
Astraea-authored `REP MOVSB` bytes (`f3a4`) through a **separate**
version-locked Unicorn 2.1.4 Python environment. It emits a finite v1
source-owned text transcript only after comparing **all 20 registered
general/context fields** and the selected final guest bytes against
independently specified expectations, including source preservation and
a read-only-page fault's unchanged bytes. Two cases are intentionally
fixed: a four-byte complete repeat, and a one-byte committed prefix
followed by a denied second write and restartable RIP/RCX/RSI/RDI.

The separately compiled
`astraea_c1_unicorn_effect_bridge_research` consumes this **untrusted**
transcript, refuses unexpected fields/state/writes, replays the expected
finished bytes into *distinct* Astraea-owned virtual guest memory, and
records them through `OwnedCpuEffectLedger`. The bridge never links to
Unicorn, never loads external ELF, and never executes source-authored
instructions natively; it checks the evidence interchange, not a
production CPU-engine API. Negative tests corrupt a committed write
and a partial-fault outcome and require refusal.

**Crucial limitation:** this two-process study does not independently
prove causality for arbitrary writes, does not expose a generic post-commit
callback, and does not validate general string instruction behavior,
concurrency, aliasing, large REP sequences or exception delivery. The
C++ verifier **models** the already-observed effects by applying known
bytes; it does not execute REP itself. Only the disposable Unicorn process
executes the fixed bytes. Both source implementation and runtime-ABI
adoption remain HOLD pending broader tests, licensing, performance,
portable embedding and security qualification.
