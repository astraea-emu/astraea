# Retail execution supervisor research design

**Status:** design candidate for C0 / ADR 0010  
**Date:** 2026-09-24  
**Issue:** #206

## Purpose

Define the smallest safe execution boundary that lets Astraea begin using a
legally owned retail PS5 executable as a diagnostic workload without treating
that executable as trusted host code.

This design preserves Astraea's existing native x86-64 advantage while making
guest syscalls, faults, mappings, and host-resource exposure explicit.

It does **not** enable retail execution by itself.

## Threat model

The retail guest worker must be treated as untrusted.

A malformed or unexpected guest executable may:

- execute an x86-64 `SYSCALL` instruction with a Prospero/FreeBSD-derived
  syscall number that overlaps an unrelated host syscall number;
- dereference invalid or hostile guest pointers;
- execute an unsupported instruction or fault intentionally;
- loop indefinitely;
- attempt to discover inherited file descriptors/handles;
- attempt filesystem or network access through accidental host ABI fallthrough;
- corrupt emulator control state if it shares an address space with the
  controller;
- create threads or change memory protections in ways Astraea has not modeled;
- trigger a bug in a partially implemented HLE service.

C0 is successful only if these failures terminate or stop the guest workload
without silently turning into ordinary host process behavior.

The threat model does not claim protection against a malicious guest escaping a
host-kernel sandbox through an operating-system vulnerability. It is an
emulator containment boundary, not a replacement for host OS security.

## Process architecture

Use two process roles.

### Controller

The normal Astraea process owns:

- loader/orchestration policy;
- verified HLE/syscall service implementations;
- compatibility-run configuration;
- trace collection and durable diagnostic output;
- timeout/resource policy;
- child-process lifetime;
- explicit host filesystem/network policy.

The controller must never directly jump into arbitrary retail guest code.

### Guest worker

A separate worker process owns:

- guest virtual-memory mappings;
- guest CPU register context;
- native x86-64 execution;
- trap/fault capture;
- a minimal IPC endpoint to the controller;
- no unrelated host resources.

The worker begins with the narrowest possible inherited capability set.

At minimum, close or prevent inheritance of unrelated descriptors/handles.
Network and filesystem access are denied unless a later verified guest service
explicitly brokers them through the controller.

## Controller/worker protocol

Use an explicit versioned protocol.

Initial logical messages:

```text
Controller -> Worker
    HELLO(protocol_version)
    MAP_REGION(guest_va, byte_count, protection, image_region_id)
    WRITE_REGION(region_id, offset, bytes)
    SET_CONTEXT(register_file)
    RUN(execution_budget)

Worker -> Controller
    READY(protocol_version)
    STOP(reason, register_file)
    SYSCALL(request_id, guest_number, args, guest_rip)
    FAULT(kind, guest_rip, fault_address)
    EXIT(return_state)

Controller -> Worker
    SYSCALL_RESULT(request_id, return_value, errno_value, output_writes)
    RESUME(register_file_delta)
    TERMINATE(reason)
```

The first implementation can use a simpler local IPC transport, but the message
semantics must remain typed and testable.

Guest pointers transmitted in syscall/HLE requests remain guest addresses.
The controller validates them through explicit guest-memory access; they are
never reinterpreted as controller-process pointers.

## Syscall interception requirement

A guest `SYSCALL` must be intercepted **before the host kernel performs the
numerically corresponding host syscall**.

This is the central C0 invariant.

Astraea must not rely on syscall-number coincidence.

### Initial cross-platform proof: exact two-byte trap substitution

For the first owned synthetic C0 proof, use a known executable location
containing an intentional x86-64 `SYSCALL` instruction:

```text
0F 05    SYSCALL
```

Replace only that known instruction with the same-length guaranteed-invalid
instruction:

```text
0F 0B    UD2
```

The worker's platform fault handler recognizes a trap at a registered patched
site, reconstructs the original guest syscall request, sends it to the
controller, receives a deterministic result, advances/resumes guest state as if
the guest syscall boundary had completed, and continues execution.

The patch-site table records:

- guest RIP;
- original bytes;
- trap bytes;
- patch provenance;
- expected instruction length.

For this first proof there is no heuristic scan of executable pages.

### Why this is only the first proof

Blindly searching executable bytes for `0F 05` is not a production strategy:
those bytes can appear inside other instruction encodings or embedded data.

A retail-capable implementation must identify executable syscall instructions
through a validated decoder/control-flow process, executable-page
instrumentation, a kernel-mediated trap mechanism, or a future JIT.

## Linux implementation candidates

C0 keeps the portable syscall request/result semantics independent from the
host interception mechanism.

### Candidate A — kernel-mediated seccomp trap/user notification

A Linux worker can use seccomp to prevent selected syscalls from executing.
A robust form would distinguish guest-originated syscall sites from worker
runtime syscalls, for example through instruction-pointer-aware policy and a
dedicated listener/supervisor channel.

Advantages:

- mediation occurs before normal syscall execution;
- kernel-enforced boundary;
- natural controller/worker separation.

Costs/risks:

- filter construction and guest-code address changes are non-trivial;
- the worker itself still needs a tiny host syscall allowlist;
- portability is Linux-only;
- user-notification semantics and lifecycle require careful race handling.

This is a strong candidate for later Linux retail execution, not required for
the first cross-platform synthetic C0 proof.

### Candidate B — ptrace syscall stops

A supervising process can trace syscall entry/exit and substitute behavior.

Advantages:

- mature Linux debugging primitive;
- naturally external supervisor;
- does not require rewriting every guest syscall site.

Costs:

- high transition overhead;
- thread lifecycle is more complex;
- interaction with signals/faults needs explicit policy.

Useful as a correctness/reference backend even if it is not the eventual fast
path.

### Candidate C — validated instruction rewriting

Decode executable guest code and rewrite guest `SYSCALL` instructions to trap
sites.

Advantages:

- conceptually portable to Linux and Windows;
- fast normal execution between traps;
- first synthetic C0 proof is a bounded version of this model.

Costs:

- requires complete tracking of executable mappings;
- must handle newly generated/self-modified executable code;
- decoder correctness becomes security-relevant;
- code hashing/self-inspection may observe patched bytes unless a shadow-code
  strategy is introduced.

Do not select a production mechanism until the synthetic proof demonstrates the
controller/worker contract.

## Windows implementation candidates

Windows has no direct equivalent of Linux seccomp for this use.

The initial proof therefore uses the same explicit `SYSCALL -> UD2` registered
patch site and a worker exception handler.

For broader coverage, candidates include:

- validated executable-page instruction rewriting with vectored exception
  handling;
- a future translation/JIT boundary;
- platform sandbox/process controls around the worker.

Windows Job Objects can be evaluated for lifecycle/resource limits. Any
filesystem/network sandbox must be treated as a separate host containment
layer from guest syscall semantics.

Do not make Windows syscall interception depend on raw guest `SYSCALL`
fallthrough.

## Guest memory model

Retail worker mappings are created from the existing guest-image/memory-plan
concepts, but ownership is worker-local.

Required invariants:

- guest virtual addresses remain guest-domain values;
- executable/writable permissions are explicit;
- W^X policy is documented for each mapped range;
- controller reads/writes use typed IPC operations or controlled shared-memory
  windows, never pointers borrowed from the worker;
- a failed guest-memory preflight performs no partial HLE write;
- executable mapping creation/removal is observable so trap/instrumentation
  state can be updated.

## Thread and TLS model

The first C0 proof is single-threaded.

Before a retail title can create arbitrary guest threads, add:

- worker-owned guest thread IDs distinct from host IDs;
- guest register context per thread;
- guest TLS base/state;
- deterministic thread creation/exit events;
- syscall/HLE requests tagged with guest thread identity;
- stop-the-world behavior for fatal faults and diagnostics.

Do not map a guest thread directly to an unmanaged detached host thread.

## Fault model

Every stop must be typed.

Initial stop reasons:

- normal guest return;
- intercepted syscall;
- unsupported syscall;
- access violation/page fault;
- illegal/unsupported instruction;
- execution budget exhausted;
- controller-requested termination;
- worker protocol failure.

A fault report contains at least:

- guest RIP;
- guest register snapshot;
- fault address where applicable;
- current guest thread;
- last bounded trace/event identifiers.

Unknown faults stop deterministically. They do not return success.

## Execution budget

Every run request has a finite budget.

The first implementation may use:

- wall-clock watchdog enforced by the controller; and/or
- explicit bounded probe structure.

Long-term retail diagnostics should additionally support deterministic stop
points such as:

- maximum syscall/HLE events;
- maximum trace events;
- breakpoint/first-unsupported boundary.

The controller must always be able to terminate the worker.

## Host resource exposure

### File descriptors / handles

The worker inherits only the explicit IPC/synchronization resources required to
communicate with the controller.

No repository files, terminal descriptors, sockets, or unrelated application
handles should be inherited by default.

### Filesystem

Guest filesystem services are brokered by HLE through explicit virtual paths.
The worker does not receive ambient access to the host filesystem merely
because guest code runs natively.

### Network

Network is disabled for the first retail diagnostics.

Any future guest network service is brokered deliberately by an HLE component
with separate policy.

## First synthetic C0 proof

Create an Astraea-owned x86-64 guest fixture whose controlled function:

1. sets a known guest syscall number and six known argument registers;
2. executes one known `SYSCALL`;
3. consumes the synthetic return value;
4. returns a deterministic final value to Astraea.

Test sequence:

```text
controller launches worker
    -> worker maps owned fixture
    -> registered SYSCALL site is replaced by UD2
    -> worker begins native execution
    -> UD2 trap occurs at the exact registered guest RIP
    -> worker sends typed SYSCALL request
    -> controller verifies number + args
    -> controller returns synthetic result
    -> worker resumes after the original syscall boundary
    -> owned fixture returns expected final value
    -> worker exits cleanly
```

The proof fails if:

- the host kernel observes/executes the guest syscall as an ordinary syscall;
- the trap occurs at an unregistered site;
- argument registers differ;
- the worker cannot be terminated;
- a fault escapes into the controller;
- the guest can access an unintended inherited host resource.

## Incremental implementation order

1. Define portable protocol structs and stop reasons.
2. Launch a separate worker with one IPC channel and deterministic teardown.
3. Move the existing owned native transition fixture into the worker without
   syscall interception.
4. Add one explicit registered `SYSCALL -> UD2` patch site.
5. Round-trip one synthetic syscall request/result.
6. Add fault/crash reporting.
7. Add resource/time limits and inherited-resource audit.
8. Evaluate Linux kernel-mediated interception against validated rewriting.
9. Add guest-thread/TLS support only when a real diagnostic workload requires
   it.
10. Admit a legally owned retail title only after the C0 acceptance gate is
    satisfied.

## Acceptance mapping for #206

- **Threat model documented:** this document.
- **Worker/control protocol specified:** message contract above.
- **Linux strategy evaluated:** seccomp mediation, ptrace reference path, and
  validated rewriting are separated from portable semantics.
- **Windows strategy evaluated:** registered rewriting/trap proof first, then
  validated executable instrumentation or future translation.
- **Owned synthetic syscall probe identified:** exact single-site `SYSCALL ->
  UD2` round trip.
- **Existing trusted-probe backend preserved:** C0 adds a separate worker path;
  it does not remove the current in-process owned-probe path.
- **Retail remains disabled until gate completion:** required by ADR 0010.

## Non-goals

This design does not:

- implement PS5 syscall numbers;
- claim a full sandbox;
- enable retail execution;
- add game-specific patches;
- require a JIT;
- define guest filesystem/network APIs;
- weaken the existing owned-probe native execution contract.
