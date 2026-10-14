# Breakout C1 — source-authenticated pre-instruction import stop

**Date:** 2026-10-14  
**Classification:** external homebrew, bounded **mediated CPU research** only.
Not a Sony service implementation, PS5 process ABI, native sandbox,
commercial-title execution or homebrew game boot.

## The precise next execution boundary

Merged PR #468 proved that a single Unicorn `emu_start(...,count=13)`
faithfully executes the thirteen original, pinned SharpEmu Breakout
instructions. The limitation was that the study stopped solely because the
instruction count reached 13; it did not independently observe an execution
engine arriving at the *next* unresolved imported-call boundary.

The new `stop_breakout_unresolved_import.py` requests a larger finite
instruction budget (32 by default; 14–64 allowed) and installs a
**pre-instruction** mediated Unicorn code hook. The hook:

1. Observes the same thirteen source-backed RIPs in the right order.
2. On the very next fetch at `RIP=0x23c`, decodes the exact six-byte
   `FF 15 rel32` indirect call and checks that its RIP-relative guest
   memory operand is exactly `GOT 0x68c8`.
3. Checks that the original first GOT word matches the unchanged source-backed
   virtual ELF image and captures the full pre-call guest CPU state.
4. Invokes the engine's stop request **without retiring the call**,
   synthesizing a result, pushing a new stack return address or following
   the unverified pointer.
5. Checks post-run that the complete final CPU context is unchanged from
   the 13-instruction reference (with undefined AF masked appropriately),
   all ten source-backed stack-write observations and the **entire 4-KiB
   synthetic stack** match, and both the RELATIVE patch and first unbound
   GOT word remain intact.

The original independent ELF identity remains
`05c414993d1cd9e7182bbf5647be1ffad72c6ed6b6f5399d01af27281fef48b8`.
The source-authored name associated with NID `Vo5V8KAwCmk` is
`sceSystemServiceHideSplashScreen` under `libSceSystemService`, per the
frozen SharpEmu author's `build.py`. This association **does not** prove
that a complete PS5 loader can resolve the bare NID to a system module.

## Important behavior uncertainty

Independent community PS5 video-output research associates
`sceSystemServiceHideSplashScreen()` with display/compositor state and
buffer registration (e.g., the public `ps5-moonlight` research notes).
This is **third-party source evidence about an execution context**, not
a direct firmware-independent Sony ABI specification or a validated Astraea
service implementation. In particular, discarding the function's return
at the selected Breakout call site does **not** make the state effects
optional or authorize a constant return-zero provider.

Before the service can be bound, qualify the exact runtime provider identity,
argument/return/error ABI, process/video session preconditions, side effects,
and deterministic lifetime/rollback semantics. Keep #410 native containment
and #334 real normal-title hardware-observation dependency independent.

## Acceptance and limitations

- Only original bytes, real RELATIVE, immutable unresolved GOT and synthetic
  startup context are admitted.
- Reject budgets too small to observe the first imported call or larger
  than the approved cap, modified original ELF, wrong stop reference or
  changed first imported GOT word.
- Retain 13 completed instructions and **zero retired imported calls**.
- The code hook is an *early boundary observer*, not a general security
  monitor. No generalized scheduler, syscall mediation, fail-safe instruction
  callback behavior for arbitrary opcodes, or runtime service implementation
  is implied.
- The pinned Unicorn dependency remains external to release builds with
  separate version-specific licensing and platform suitability review.

The next integration goal is an independently evidenced, stateful service
contract **and** a production-suitable mediated CPU/memory backend, not a
guessed HLE stub or native execution shortcut.
