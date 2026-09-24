# ADR 0010: Require a supervised retail guest-execution boundary

**Status:** Accepted  
**Date:** 2026-09-24

## Implementation status

The Linux x86-64 diagnostic-admission profile is implemented:

- controller and guest worker are separate processes with bounded typed IPC;
- worker lifetime, time/CPU/address-space/descriptor/core/file-growth limits,
  and inherited-resource policy are explicit;
- artifact authority crosses through a sealed immutable anonymous file
  descriptor rather than the original host pathname;
- guest-originated raw syscalls are contained before host-kernel execution by
  an instruction-pointer-scoped seccomp/SIGSYS path and are normalized only
  after ordinary code verifies the literal guest syscall instruction/RIP;
- typed syscall, fault, diagnostic, STOP, and TERMINATE ordering is enforced;
- the production Linux diagnostic stops before retail native entry when
  process-entry prerequisites are unsupported.

The current first load-bearing stop is the unverified PS5 initial-process ABI.
Astraea therefore does **not** yet claim arbitrary retail native instruction
execution, boot, or compatibility. Windows retail admission remains unsupported
even though trusted owned-probe worker/native execution exists there.

## Context

Astraea's current native x86-64 backend deliberately executes only trusted,
Astraea-owned synthetic probes.

A PS5 retail executable is not equivalent to an ordinary host x86-64 program.
In particular:

- raw guest `SYSCALL` instructions target the guest FreeBSD/Prospero-derived
  ABI rather than the host Linux/Windows syscall ABI;
- guest TLS, thread state, signals/exceptions, and process assumptions differ
  from the host;
- arbitrary guest pointers and mappings are untrusted;
- a native execution bug must not give guest code uncontrolled access to the
  emulator process, host filesystem, network, or unrelated memory.

Booting retail code before defining this boundary would turn native execution
from an optimization into an accidental trust decision.

## Decision

Before arbitrary retail guest execution is enabled, Astraea will implement a
supervised retail execution boundary.

### Owned probes remain narrow

The existing in-process native backend remains valid for trusted Astraea-owned
synthetic probes under ADR 0005.

This ADR does not require rewriting that path.

### Retail execution supervisor

Retail execution must occur behind an explicit supervisor/worker boundary with
a documented threat model.

The design must provide:

- guest instruction execution isolated from the controlling process strongly
  enough that a guest crash/fault does not corrupt Astraea control state;
- interception/translation of guest raw syscalls before they can be interpreted
  as host syscalls;
- a typed Astraea syscall/HLE dispatch boundary;
- guest TLS and thread lifecycle ownership;
- controlled memory-map/protection policy;
- deterministic stop/fault reporting;
- CPU/runtime/resource limits sufficient to recover from runaway guest code;
- explicit filesystem exposure policy;
- explicit network policy;
- no implicit inheritance of host process privileges or unrelated file
  descriptors;
- traceable unsupported-syscall/HLE stops rather than silent success.

The preferred architecture is a separate guest worker process supervised by
the main Astraea process. Platform-specific mechanisms may differ.

### Syscall strategy is backend-specific, guest ABI is not

The portable layer defines guest syscall request/result semantics.

Linux may use mechanisms such as syscall trapping/interception, controlled
instruction rewriting, or another reviewed mechanism. Windows may require a
different exception/instrumentation strategy.

The exact mechanism is an implementation decision. The architectural
requirement is that guest syscalls never accidentally become ordinary host
syscalls.

### Compatibility testing gate

A commercial title may be admitted as a diagnostic workload only when:

- its use is lawful and no redistributable retail content is committed;
- the supervised execution boundary is active;
- the required loader/import path can stop deterministically on unsupported
  behavior;
- logs identify the first unsupported dependency;
- title-specific patches are not used as substitutes for missing semantics.

Commercial-title progress is an integration signal, not a correctness oracle.

## Consequences

### Positive

- Native x86-64 remains a performance advantage without treating retail code as
  trusted host code.
- Raw guest syscalls have an explicit semantic home.
- Crash/fault containment improves debugging and security.
- Game-driven development can begin without weakening the verification-first
  architecture.

### Negative / constraints

- Retail testing begins later than a direct in-process launch hack.
- Platform-specific supervisor/syscall mechanisms require substantial systems
  work.
- Some existing native-execution assumptions may need to become explicit
  worker-process interfaces.

## Alternatives considered

### Execute retail binaries directly in the current process

Rejected. The current backend is intentionally not a sandbox and raw syscall,
TLS, fault, and host-resource boundaries are not defined for untrusted code.

### Build a full x86-64 JIT before retail testing

Rejected as a mandatory first step. Native execution can remain viable on
x86-64 hosts if the syscall/isolation boundary is correct.

### Return success for unknown syscalls/HLE calls

Rejected because it hides the first real missing dependency and produces
non-local failures.

## Evidence / references

- ADR 0002: host and execution strategy.
- ADR 0005: guarded native x86-64 execution.
- ADR 0006: dependency-driven vertical integration.
- Public PS5 compatibility projects demonstrate dedicated guest syscall/HLE
  dispatch rather than allowing guest syscall numbers to fall through to host
  semantics.

## Revisit triggers

Revisit if:

- a different execution architecture (for example a JIT/interpreter) becomes
  the primary runtime;
- the separate-process boundary prevents required guest semantics and a
  stronger alternative is demonstrated;
- host platform sandboxing makes a different containment boundary materially
  safer or simpler.
