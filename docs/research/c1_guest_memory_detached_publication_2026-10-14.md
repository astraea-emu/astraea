# C1 — Detached guest-memory publication after mediated CPU execution

**Date:** 2026-10-14  
**Scope:** research-only guest-memory **publication** boundary, not a
general runtime MMU, hostile-code sandbox or arbitrary instruction mediator.

## Architectural distinction: execution vs publication

The pinned external Unicorn 2.1.4 research engine maps guest pages into a
*private* emulator-owned virtual address space. Its memory-write callback
observes attempted guest effects; it is not an independently proven
post-commit callback and cannot safely enforce original sub-page rights
while the emulator's storage mappings use permission unions.

Instead of pretending those hooks are a security mechanism, the new
`owned_guest_memory_publication.py` uses a **two-phase, fail-closed**
controller-side transaction:

1. Copy a bounded original guest-page inventory into **immutable bytes**.
   Accept at most 256 distinct, 4-KiB-aligned virtual pages.
2. After a source-owned guest run and a *separately attested* terminal
   outcome, receive an ordered list of proposed committed-write events,
   at most 64 events with at most 64 bytes each.
3. Preflight every proposed write against the already merged, original
   `GuestRegionPolicy` **byte ranges**, including RX/RW mixed host pages,
   zero-permission source ranges, cross-region boundaries, holes and
   address overflow. Reconstruct expected output bytes in a private
   controller buffer, preserving the precise event order and overlap.
4. Compare **every byte of every privately executed guest page** against
   the expected output and reject any missing/extra page, forged/unwritten
   event, unreported mutation or illegal access. A write hook alone never
   qualifies the event.
5. Only after verification may `publish()` expose detached, immutable
   copies; a failed or prematurely used transaction is permanently
   poisoned. The controller must never publish the engine's raw mutable
   page handles.

Partial-fault publication remains **explicitly refused**, even if a write
prefix was observed, until independent tests qualify a source-authenticated
restartable CPU register state and actual committed prefix provenance.
The first accepted terminal outcomes are `retired` and the explicitly
attested, source-owned `typed_invalid_instruction`.

## Tests and real source-built ELF bridge

The new source-owned test suite covers 25 bounded positive and negative
outcomes: single and ordered overlapping/multiple writes, adjacent
cross-page spans, original RX and no-access rejects (including mixed-page
neighbours), unmapped holes, record payload disagreement, write-hook-only
guesses, undeclared mutations, forged extra/missing pages, invalid
length/page address, oversized event sets, unqualified partial faults,
unattested terminal, single-use and immutable publication snapshots.

The **same full source-built PS5-format ELF** already merged in #471
(SHA-256 `3a69a4a035a10656c1532b1b8825eb4e361d2f755ac356ddeb7a36932f5c8cd7`)
continues to execute:

```asm
mov eax, 42
mov qword ptr [rip + 0xc0c4], rax
ud2
```

The original linked write goes to virtual guest `0xc0e0`. The research
verifier independently checks its exact executable-source bytes,
eight RELATIVE relocations, original writable guest-byte range, one
observed eight-byte store of value 42, full register state and a real
terminal invalid-instruction exception. Only **after** those checks does
the publication transaction compare the full mapped image and expose an
immutable, verified copy. Two separately instantiated engines must agree.

The external public toolchain is pinned and the compiled ELF stays outside
GitHub source history. No Sony service return value or runtime state is
fabricated to advance Breakout.

## Limits and next security gate

This protects the **controller's accepted guest-memory snapshot**, not the
host process or any external side effects the mediated engine might attempt
during execution. It cannot roll back arbitrary host syscalls, GPU I/O, file
access, dynamically created guest memory, partial fault state or malicious
code exploiting the research engine. No untrusted guest is admitted under
this contract. Memory-access callback ordering is not evidence of a
general instruction-time write barrier, and matching final bytes cannot
prove an unchanged-value write's causal origin.

A true runtime backend still requires source-independent memory isolation,
guest syscall/host capability confinement, accurate partial-fault/restart
handling, thread/memory alias semantics, reliable post-commit effects,
cost/portability and licensing review under #461 and #410. The first
independent Breakout service remains unresolved at `0x23c` (#378).
