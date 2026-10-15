# C1 supervised original Breakout pre-import stop — 2026-10-15

**Category:** Unchanged, independently authored, source-pinned PS5-marked raw ELF under a mediated research-only x86-64 engine. **Not a commercial PS5 executable. Not a production CPU backend.**

## Motivation

Merged #469 stopped the original SharpEmu Breakout ELF after 13 real source-backed guest instructions at the first unresolved Sony imported CALL. Merged #476 and #477 separately qualified a bounded Linux child process, a limited syscall-denial filter and controller-side bounded output capture, but those tests used an Astraea-authored instruction fixture. This gate connects the actual independent Breakout dependency frontier with that supervised execution experiment.

The source ELF is SHA-256 `05c414993d1cd9e7182bbf5647be1ffad72c6ed6b6f5399d01af27281fef48b8`, source revision `49e6b3b25678f14ec57a70b907ee215fddb608d8`. Its synthetic trace must match the pinned reference: 13 pre-import instructions, 10 observed guest stack writes and an unretired indirect import at RIP `0x23c`, GOT slot `0x68c8`.

## What is new

The existing `stop_breakout_unresolved_import.py` exposes an opt-in `--restricted-worker` mode. After checked original memory, relocation, synthetic process state and observer hooks are staged—but **before** mediated CPU execution—the child installs the already qualified Linux x86-64 partial seccomp policy from #476 and proves selected kernel-denied open/socket/fork operations. The feature is off by default to preserve existing behavior.

A separate `supervise_breakout_import_stop.py` parent:

- Authenticates the frozen independent source ELF SHA-256 and parses/bounds the existing Astraea source-owned trace before spawning.
- Launches two fresh research-only Unicorn children with `RLIMIT_CPU`, `RLIMIT_AS`, `RLIMIT_NOFILE`, `RLIMIT_FSIZE`, closed inherited file descriptors and separate child process groups.
- Enforces 4096-byte stdout and stderr limits while **reading**, using #477's bounded selector-based capture.
- Refuses abnormal exit, unexpected stderr, changed/missing/extra report lines and nondeterministic reports. The expected report includes zero retired imported calls, original import RIP/GOT and the deliberately partial denial policy.
- Keeps the original ELF, relocation values, first unresolved import slot and guest-memory checks inside the pinned mediated research engine.

The C1 workflow additionally rejects intentionally altered ELF bytes, changed trace stop identity and a tampered original first GOT slot. The existing direct typed import-stop gate remains in CI as an independent regression comparison.

## Explicit limitations

The hosted child runs **mediated Unicorn**, not outside-authored native machine code. Source authenticity and exact output text checks do not establish hostile-worker report authenticity; a compromised Python or Unicorn worker could fabricate a plausible report. The current syscall policy is partial/default-allow for unspecified syscalls, and process group cleanup cannot guarantee descendants which escape the group are terminated. These unresolved security properties keep #410 open.

No genuine PS5 process-entry ABI, real Sony service provider, dynamic link lifecycle completion, graphics frame, or commercial retail first instruction is claimed. This is an integration increment in the #461 backend qualification direction, **not** acceptance of Unicorn for distribution or proof of commercial runtime readiness. #334 and #475 remain separate evidence gates. Keep ADR 0013 proposed.

## Review gate

Require both pinned C1 workflows plus Linux x64, Windows x64, macOS ARM64, sanitizer and fuzz CI green on the exact PR head. Merge only after reviewing actual logs, confirming all negative probes ran and ensuring no production or commercial admission was added.

References: [#378](https://github.com/astraea-emu/astraea/issues/378), [#410](https://github.com/astraea-emu/astraea/issues/410), [#461](https://github.com/astraea-emu/astraea/issues/461), [#469](https://github.com/astraea-emu/astraea/pull/469), [#476](https://github.com/astraea-emu/astraea/pull/476), [#477](https://github.com/astraea-emu/astraea/pull/477).
