# C1 — Research-only original guest byte-range policy

**Date:** 2026-10-14. **Status:** source-owned policy and integration candidate. Not a production CPU MMU, hostile-guest sandbox, Sony process memory model or release backend.

## Why this work is needed

The complete source-built PS5-format ELF exercised in merged #470–#471 contains two **disjoint source PT_LOAD byte ranges with different R/W/X permissions on the same host 4-KiB page**. Unicorn's virtual mapping API sets permissions on whole pages, while the ELF describes original guest permissions at finer byte extents. A storage-page **union** is therefore not a faithful guest access-permission model.

Earlier research permitted exactly one source-authored, fixed `MOV [RIP+disp32], RAX` instruction to write one original source data symbol and compared every final mapped page; this has no reusable mechanism to classify other source guest ranges.

## Reusable bounded policy

`owned_guest_byte_access_policy.py` introduces `GuestRegionPolicy`: an immutable, sorted, finite set of original guest virtual byte ranges and original R/W/X bits. It accepts validated `PT_LOAD` extents, not page-rounded or permission-unioned storage views.

`check(kind, address, width)` returns a typed decision for `read`, `write`, or `execute`, checking the entire original range before classifying it. It refuses zero/excessive widths (15-byte maximum instruction fetch, 64-byte maximum data access), address overflow, unmapped gaps, missing permissions and unknown operations. **Adjacent** original ranges can be traversed only when *every byte* grants the operation. Overlapping original mappings are rejected, as are overlarge mapping inventories; mixed host-page permissions are measured but never treated as a permission union for policy decisions.

The pure source-owned test suite qualifies 18 cases, including virtual RX and RW byte ranges sharing a physical page, cross-region reads/writes, permission breaks, holes, invalid lengths, u64 overflow and exact-range rejects. The pinned full ELF guest-store verifier now uses the same policy when checking **each original instruction fetch and the one expected eight-byte write** to source guest `0xc0e0`, retaining its separate exact code/payload and end-to-end full-memory comparison.

## Hard security limit

**A Python memory-hook callback is not a general precommit write barrier.** The broker here is a policy oracle and rejection classifier, *not* an enforceable MMU for arbitrary untrusted machine code. A denied hook may not guarantee that all effects of an in-progress guest instruction were prevented; neither observed callback order nor a final byte value proves general causality. Do **not** allow outside-authored guest code simply because these checks pass.

The source-built ELF remains hash-locked to SHA-256 `3a69a4a035a10656c1532b1b8825eb4e361d2f755ac356ddeb7a36932f5c8cd7`, contains exactly three known source instructions (`MOV EAX,42; MOV [RIP+disp32],RAX; UD2`), and is executed only inside disposable research Unicorn 2.1.4 virtual memory. All mapped bytes are compared to the expected one-store result before publishing a positive outcome; the terminal `UD2` must raise a typed invalid-instruction error.

## Next gate beyond this increment

A general mediated execution adapter needs **enforceable** mapping protection for arbitrary guest effects, including implicit stack/vector/string writes, cross-region effects, partial instruction faults, and aliases. The controller must avoid exposing memory/state from a denied run. Qualify a complete read/write/fault workload against an independently justified write-commit ledger, then review host containment, third-party engine license and host portability before any production adoption. Breakout remains blocked at the authentic `sceSystemServiceHideSplashScreen` dependency (issue #378); native arbitrary guest admission remains blocked (#410).

**Early CI finding:** The first PR head exposed a legitimate source ELF `PT_LOAD` extent with `p_flags=0`. Rejecting it during policy construction was incorrect: the guest range exists but must grant **no** R/W/X operations. The corrected policy preserves this extent and denies every attempted access. A new regression covers source RX and mapped-inaccessible extents sharing one backing page. This does not widen guest permissions.
