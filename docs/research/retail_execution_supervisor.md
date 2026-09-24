# Retail execution supervisor

**Status:** Linux x86-64 diagnostic-admission profile implemented  
**Date:** 2026-09-24  
**Design issue:** #206  
**Current admission work:** #260 / #286  
**Post-C0 hardening:** #297 / #298

## Purpose

Record Astraea's supervised retail-execution threat model, the implementation
selected for the first Linux diagnostic path, and the boundaries that remain
unsupported.

The goal is not to make arbitrary PS5 code "trusted." It is to let Astraea
accept a lawfully obtained executable as a bounded diagnostic input without
allowing raw guest behavior to silently become ordinary host-process behavior.

This note is the implementation follow-through to ADR 0010. Earlier design
alternatives remain available in Git history and closed PR/issue discussion;
the sections below describe the current selected profile.

## Threat model

The retail guest worker is untrusted emulator input.

A malformed or unexpected guest executable may:

- issue a Prospero/FreeBSD-derived x86-64 `SYSCALL` number that overlaps an
  unrelated Linux syscall;
- dereference invalid or hostile guest pointers;
- fault or execute unsupported instructions;
- loop indefinitely;
- attempt to discover inherited descriptors;
- attempt ambient filesystem/network/IPC access;
- create threads, TLS state, or executable mappings Astraea has not modeled;
- trigger a bug in a partially implemented parser/HLE/runtime service.

The containment boundary is intended to make these conditions stop or fail
deterministically. It does **not** claim protection against a hostile guest
exploiting a host-kernel vulnerability; this is an emulator containment model,
not a replacement for host OS security.

## Process architecture

### Controller

The normal Astraea process owns:

- the user-selected host pathname and bounded artifact read;
- compatibility-run policy;
- worker creation/lifetime/resource policy;
- typed syscall/HLE services when explicitly enabled;
- durable diagnostic output;
- future explicit filesystem/network brokerage.

The controller never directly jumps into retail guest code.

### Worker

A separate worker owns:

- worker-local guest mappings/context when native entry is admitted;
- fault/trap capture;
- the bounded control channel;
- the sealed artifact copy;
- no unrelated inherited host resources.

For the production diagnostic, the controller seals the exact admitted artifact
bytes into an anonymous memfd and exposes only that object to the child on the
fixed artifact descriptor. The worker verifies the required seals, copies the
bytes, closes the descriptor, and only then participates in RUN.

The original host pathname never crosses this interface.

## Implemented control protocol

The portable wire format uses a fixed 12-byte little-endian envelope and
bounded fixed-width payloads. Native C++ struct layout is never copied onto the
wire.

The first production message flow is:

```text
controller
    -> HELLO
worker
    -> READY
controller
    -> RUN_REQUEST
worker
    -> zero or more SYSCALL_REQUEST
controller
    -> matching SYSCALL_RESULT
worker
    -> optional terminal FAULT or DIAGNOSTIC
    -> matching STOP
controller
    -> TERMINATE
worker
    -> deterministic exit/reap
```

Terminal diagnostics and faults are ordered protocol states, not arbitrary
strings or exit-code conventions.

## Linux syscall containment

### Selected mechanism

Linux retail-capable native entry uses an instruction-pointer-scoped seccomp
filter that returns `SECCOMP_RET_TRAP` for syscall entry associated with the
guest executable range.

The kernel therefore generates `SIGSYS` without executing the selected host
syscall. Ordinary Astraea code then normalizes the event fail-closed:

- reconstruct the candidate x86 call site from the captured post-instruction
  context RIP;
- require the literal guest instruction bytes to be an admitted syscall form;
- require those bytes to lie inside the exact executable guest mapping;
- require kernel-reported instruction-pointer metadata to agree with the
  verified call-site/post-instruction interpretation;
- project only then into the portable typed guest syscall request.

The controller validates request/result identity. Resume changes only the
architecturally intended result state for the supported proof.

This mechanism is Linux-specific; the portable guest syscall semantics are not.

### Why the earlier UD2 proof still matters

Astraea first proved registered, exact `SYSCALL -> UD2` substitution for a
known owned trap site. That remains a useful bounded instrumentation model and
Windows research building block.

It is **not** the selected Linux retail-wide mechanism. Astraea does not scan
arbitrary executable bytes heuristically for `0F 05` and rewrite them.

### Alternatives retained as references

- **ptrace syscall stops:** useful as a possible correctness/reference path,
  but higher transition and thread/signal complexity.
- **validated code rewriting:** potentially useful on platforms without
  seccomp, but executable mapping/self-modification and decoder correctness
  become security-relevant.
- **JIT/interpreter:** not required merely to reach the first x86-64 retail
  diagnostic; revisit if native execution stops being the primary runtime.

## Faults, stops, and diagnostics

The worker reports typed state rather than "best effort" continuation.

The control plane distinguishes:

- normal stop;
- trapped syscall;
- unsupported syscall;
- access violation / illegal instruction / other typed guest fault;
- execution/resource termination;
- protocol failure;
- diagnostic boundary.

The retail diagnostic vocabulary additionally identifies bounded pre-entry
failures such as:

- loader rejection;
- non-executable entry;
- SCE dynamic metadata rejection;
- unresolved dependencies;
- unapplied relocations;
- unsupported TLS;
- unsupported PS5 initial-process ABI;
- native-backend error.

A terminal diagnostic must be followed by the matching
`STOP(diagnostic_boundary)`. A resumable syscall after a terminal diagnostic,
or a diagnostic STOP without the diagnostic event, is a protocol failure.

## Resource and authority policy

Before guest RUN, the controller installs finite worker ceilings.

The current Linux production diagnostic uses bounded:

- wall-clock controller timeout;
- kernel CPU time;
- address-space commitment ceiling;
- open-file descriptor count;
- core dumps disabled;
- file growth disabled.

The worker inherits only the resources intentionally required for its control
channel/artifact handoff. No generic filesystem or network syscall service is
configured for the first retail diagnostic.

These ceilings are containment policy, **not** emulated PS5 hardware sizes.

## Current production diagnostic

The user-facing Linux x86-64 surface is:

```text
astraea diagnose <artifact>
```

The controller:

1. opens the user-selected artifact once;
2. sizes and reads through that same opened file object;
3. rejects empty/oversized/moving inputs explicitly;
4. hands immutable bytes to the worker;
5. applies resource policy before RUN;
6. returns stable typed `boundary=` / `stage=` output.

The worker:

1. validates/copies/closes the sealed artifact descriptor;
2. builds the existing PS5/SCE `GuestImage`;
3. reports the first known pre-entry obligation;
4. never uses the synthetic owned-probe initial stack as if it were the PS5
   process-entry ABI.

An otherwise-ready synthetic PS5/SCE fixture therefore stops at:

```text
unsupported_initial_process_abi
```

with zero syscall requests and no manufactured native fault.

That is the correct current diagnostic result. It is not a game-boot failure
being hidden; the missing process-entry contract is the next real dependency.

## Windows status

Windows has:

- trusted owned-probe native execution;
- the process-worker/control primitives;
- resource/lifetime containment mechanisms used by those probes.

Windows does **not** currently admit the production retail diagnostic. A
Windows-wide raw guest-syscall interception strategy must be independently
justified before that changes.

## Remaining runtime work

The first compatibility-driven loop begins after the production diagnostic is
merged and run against a lawfully obtained artifact.

The first currently known load-bearing research question is the PS5
initial-process ABI. Subsequent dependencies should be pulled only when they
become the earliest diagnostic blocker: module dependencies, relocations, TLS,
HLE/syscalls, wider memory/thread semantics, GPU state, and presentation.

## Non-blocking Linux hardening

Two follow-ups are intentionally outside the first-diagnostic critical path:

- **#297 pidfd supervision:** evaluate stable pidfd-backed worker identity,
  signalling, and pollable exit handling.
- **#298 Landlock confinement:** evaluate an optional unprivileged
  filesystem/network/IPC restriction layer without replacing guest syscall
  containment.

These improve defense in depth; neither changes the portable guest ABI.

## Evidence / references

Primary host-containment references include:

- Linux kernel seccomp filter documentation, especially
  `SECCOMP_RET_TRAP` / `SIGSYS` pre-execution semantics;
- Linux pidfd process-lifetime interfaces;
- Linux Landlock unprivileged access-control documentation.

Project references:

- ADR 0005 — guarded native owned-probe execution;
- ADR 0010 — supervised retail execution requirement;
- #260 — C0 acceptance / first retail diagnostic;
- #286 — production diagnostic runner;
- #297 / #298 — post-C0 Linux hardening.

Public emulator projects may be used as comparative evidence for architectural
concepts, but their implementation is not copied into Astraea.

## Non-goals

This design does not:

- implement the PS5 syscall table;
- claim a complete hostile-code sandbox;
- provide firmware, keys, proprietary SDK material, retail content, or system
  modules;
- define a DRM/authentication bypass path;
- claim a retail title boots/renders/is playable;
- require a JIT;
- define the PS5 initial-process ABI by analogy.
