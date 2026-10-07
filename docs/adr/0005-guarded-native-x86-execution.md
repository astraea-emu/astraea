# ADR 0005: Guarded native x86-64 execution boundary
**Implementation status (2026-09-24):** This ADR remains the contract for trusted Astraea-owned native probes. Retail diagnostics now use ADR 0010's separate supervised worker; the historical statements below that retail execution was not yet eligible describe the pre-C0 state and are retained as decision context.


**Status:** Proposed  
**Date:** 2026-09-21

## Context

M1 produces a deterministic owned `GuestImage` but does not execute it.

The PS5 CPU ISA is x86-64, so Astraea can initially execute carefully controlled guest instructions natively on x86-64 hosts instead of building an interpreter/JIT first.

Native execution creates a much stronger trust boundary than parsing:

- guest instructions execute with the host process's CPU privileges
- bad guest pointers can fault in host address space
- a guest can clobber registers expected by the host ABI
- a bad transition can strand execution on a guest stack
- process-global signal/exception handlers can accidentally consume host faults
- writable+executable memory would weaken the emulator process's security posture

M2 therefore needs a narrow transition contract before production code.

## Decision

Astraea will implement native x86-64 execution behind an explicit host backend.

### Portable layer

Portable code owns:

- `GuestCpuContext`
- execution request/result types
- normalized stop/fault reasons
- mapping/protection plan metadata
- trace/HLE hook interfaces

Portable code contains no inline assembly, POSIX signal code, Windows SEH code, or host virtual-memory API calls.

### Initial execution hosts

Production M2 v0 targets:

- Linux x86-64
- Windows x86-64

macOS ARM64 continues to build/test portable execution contracts but has no native guest backend.

An x86_64 macOS build running under Rosetta may be explored as a developer convenience, but Rosetta is not Astraea's architectural correctness oracle and is not an M2 v0 acceptance requirement.

### Trust level

M2 v0 executes only Astraea-owned synthetic probes.

Arbitrary retail/game machine code is not eligible for native execution until isolation, fault recovery, resource control, and platform policy are substantially stronger.

### Address model

The initial native backend uses identity mapping for controlled guest memory:

```text
guest virtual address == host virtual address used by guest instructions
```

This preserves unmodified x86-64 pointer semantics.

The backend must request exact guest addresses without replacing an existing host mapping.

If an address cannot be reserved safely, preparation fails.

### W^X

No guest page is writable and executable simultaneously in M2 v0.

Pages are populated under writable/non-executable host permissions, then transitioned to their final protection before entry.

If host page granularity would require one page to be both writable and executable, v0 rejects that image/profile rather than creating RWX memory.

### Transition isolation

Guest execution runs on a dedicated execution thread.

The transition stub:

1. saves host state required by the host ABI
2. installs the guest integer context
3. switches to the mapped guest stack
4. enters guest RIP
5. returns to host only through a defined stop/fault path
6. restores host state before returning to ordinary C++ code

No C++ stack unwinding crosses guest frames.

### Fault recovery

Linux and Windows have different adapters but produce one normalized `GuestFault`.

A fault is consumed only when:

- the current thread has an active Astraea execution frame, and
- the interrupted instruction pointer belongs to an active guest execution region / defined gate.

Unrelated host faults are chained/delegated normally.

Linux uses `sigaction` with `SA_SIGINFO` and a dedicated alternate signal stack. The handler reads the saved `ucontext_t`, records fixed-size fault state, and transfers control to a pre-registered recovery path without allocation, locks, logging, or C++ unwinding inside the handler.

Windows uses the platform exception context (`CONTEXT` / `EXCEPTION_POINTERS`) through a scoped execution-backend exception mechanism. It must not install a handler that silently consumes unrelated process exceptions.

### Guest context v0

The portable integer context includes:

- RAX, RBX, RCX, RDX
- RSI, RDI, RBP, RSP
- R8-R15
- RIP
- RFLAGS
- FS base
- GS base

FS/GS values exist in the representation for future platform work; the synthetic v0 profile requires them to remain zero unless a later accepted contract defines installation.

SIMD/x87/AVX architectural state is deliberately outside the first portable context. Synthetic M2 v0 probes therefore must not depend on preserved guest SIMD/x87 state.

The host backend must still preserve whatever host nonvolatile FP/vector/control state its host ABI requires.

### Synthetic entry profile

For the first probe:

- RIP comes from `GuestImage.elf.header.entry`
- RSP comes from `GuestImage.initial_stack.rsp`
- general registers are deterministic zeros unless the synthetic profile explicitly defines another value
- RBP is zero
- RDX is zero; Astraea does not synthesize an OS atexit callback
- direction flag is clear
- TLS/FS-base-dependent probes are deferred

This profile is an Astraea synthetic convention, not claimed PS5 process state.

## Consequences

### Positive

- Portable loader/HLE/trace code remains buildable on Apple Silicon.
- Native execution can reach the first end-to-end prototype quickly on x86-64 hosts.
- Guest/host faults have one normalized representation.
- Identity mapping preserves ordinary unmodified x86-64 pointer behavior.
- W^X and no-replace mapping prevent the first backend from taking avoidable security shortcuts.
- Linux and Windows can use their native fault/context facilities behind one interface.

### Negative / constraints

- Exact guest addresses may be unavailable in a host process.
- First synthetic fixtures must use deliberately chosen, page-friendly addresses.
- Mixed W/X host pages are rejected in v0.
- ARM64-native execution remains unsupported.
- SIMD/x87-dependent guest code is outside the first execution profile.
- Native execution is not a sandbox and remains inappropriate for untrusted retail binaries.

## Alternatives considered

### Execute arbitrary game code immediately

Rejected. The native backend is not yet an isolation boundary.

### Use host calls/returns directly as the guest transition

Rejected. Guest RSP is not a valid host call stack and guest code must not own the host return address.

### Keep guest addresses relocated away from their numeric values

Rejected for the first native backend because unmodified x86-64 absolute pointers would no longer have correct semantics without pervasive address translation.

### Allow RWX pages for convenience

Rejected. Synthetic probes can be constructed to obey page-level W^X; ambiguous mixed pages should fail explicitly.

### Make Rosetta the normal macOS execution backend

Rejected as a core dependency. It is useful for experiments and debugging but does not define Astraea's portable model.

## Evidence / references

- x86-64 psABI: https://gitlab.com/x86-psABIs/x86-64-ABI
- Linux sigaction / saved ucontext: https://man7.org/linux/man-pages/man2/sigaction.2.html
- Linux signal execution context: https://man7.org/linux/man-pages/man7/signal.7.html
- Microsoft x64 CONTEXT: https://learn.microsoft.com/windows/win32/api/winnt/ns-winnt-context
- Microsoft virtual-memory protection: https://learn.microsoft.com/windows/win32/api/memoryapi/nf-memoryapi-virtualprotect
- Apple Rosetta translation environment: https://developer.apple.com/documentation/apple-silicon/about-the-rosetta-translation-environment
- ADR 0002: host and execution strategy

## Revisit triggers

Revisit when:

- untrusted/retail guest execution becomes a goal
- an ARM64 interpreter/JIT backend is prioritized
- self-modifying guest code is required
- mixed W/X guest pages are observed in required workloads
- SIMD/x87 state is required by the controlled execution corpus
- a separate-process or hardware-assisted sandbox becomes justified
