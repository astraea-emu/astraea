# C1 mediated Linux child: limited capability gate (2026-10-14)

**Category:** Astraea-authored research fixture only. Not production guest execution, a commercial PS5 ELF, a Sony process entry, or a verified general-purpose security sandbox. Results must pass exact-head CI before being treated as merged evidence.

## Why this matters

The current Linux native syscall trap checks the syscall instruction IP and cannot prove that guest control flow cannot jump into allowed host code. Detached guest memory publication (#473) verifies private guest bytes, not irreversible host-side effects.

This research gate tests a different path: run source-owned x86-64 instructions in Unicorn's mediated virtual guest address space inside a separate Linux child, apply a limited kernel seccomp capability-denial policy, and have a controller check a bounded report.

## Exact fixture and verification

Six source-owned x86-64 instructions retire: MOV immediate, two eight-byte guest stores, arithmetic, a guest data read and XOR. The seventh instruction is an original UD2 that faults before retirement. Two fresh child processes must return identical outputs.

The controller checks RIP sequence, exact read and write events, full final guest data page, register state, defined flag bits and typed terminal. GuestMemoryPublication independently reconstructs the final 4-KiB guest page from an initial zero page and two ordered writes. Eight tampered report variations must fail closed. This does not establish that all reported CPU events were independently authenticated.

## Linux capability probe

The worker installs no_new_privs and seccomp-BPF after loading Unicorn. The filter intentionally distinguishes CLONE_THREAD (permitted for the mediated engine's pthreads) from fork-style clone (denied). It returns ENOSYS for clone3 to exercise glibc's legacy clone fallback; this is an engine integration concession, not hostile-code confinement. The filter rejects x32-number and non-x86-64 architecture cases, returns EPERM for selected open/socket/fork/exec and resource-management syscalls, and otherwise ALLOWS syscalls. After filter installation, genuine open, socket and fork attempts must all fail with kernel EPERM.

The parent starts a separate worker with closed nonstandard inherited FDs, disabled stdin, finite wall/CPU/address-space/file-size limits and a strict bounded JSON output.

## Known limitations — not a complete hostile-code sandbox

This syscall policy is intentionally partial and default-allow for unspecified syscall numbers; memory mapping, ioctls on existing FDs and other host effects remain possible. A hostile worker may forge reports. Source fixture exact-state checks are not cryptographic attestation. Unicorn and Python memory are not proven resistant to malicious input or engine exploitation. A write-hook callback is precommit observation, not independently proven write origin; final-page readback qualifies only this fixed workload.

No outside-authored native execution, production run command, imported Sony service or first commercial guest instruction is enabled. This does not close #410, accept an engine under #461, or advance #475 beyond C0 capability.

## Next gate

Review the exact-head Linux worker test, all seven CI jobs, and the entire PR diff. Future changes require a full host capability inventory, brokered/default-deny worker policy, stronger untrusted-report authentication and malicious boundary probes. Console interaction is neither requested nor needed.

Related: https://github.com/astraea-emu/astraea/issues/410 ; https://github.com/astraea-emu/astraea/issues/461 ; https://github.com/astraea-emu/astraea/pull/473 ; docs/adr/0013-cpu-backend-trace-evaluation.md.
