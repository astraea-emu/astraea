# Native x86-64 Guest/Host Transition Contract — M2

**Status:** Proposed  
**Related public issues:** #1, #3, #4, #5  
**Scope:** portable execution state and guarded Linux/Windows x86-64 backend boundary

## 1. Objective

Define the narrow interface between M1's validated `GuestImage` and a native x86-64 execution backend.

The first acceptance target remains a trusted synthetic probe, not a retail PS5 executable.

## 2. Layering

```text
GuestImage
   |
   v
ExecutionPlan         portable
   |
   +--> GuestCpuContext
   +--> GuestMemoryPlan
   +--> StopGate metadata
   |
   v
NativeExecutionBackend
   |
   +--> Linux x86-64 adapter
   +--> Windows x86-64 adapter
   |
   v
ExecutionResult
   |
   +--> stopped at HLE/gate
   +--> guest exit
   +--> normalized fault
```

Issue #3 defines the HLE registry/gate semantics. This document reserves a backend stop mechanism capable of handing a guest-to-host event to that layer.

## 3. Portable GuestCpuContext

M2 v0 defines explicit integer architectural state:

```text
GuestCpuContext
    rax rbx rcx rdx
    rsi rdi rbp rsp
    r8 r9 r10 r11
    r12 r13 r14 r15
    rip
    rflags
    fs_base
    gs_base
```

All fields are fixed-width `uint64_t`.

No host `ucontext_t`, Windows `CONTEXT`, compiler register structure, or assembly layout is exposed in this public type.

The portable type is order-stable and independently unit-testable.

## 4. v0 extended-state boundary

Guest SIMD/x87/MMX/AVX register state is not part of v0.

Consequences:

- synthetic v0 probes must be integer/control-flow only
- a backend must preserve host ABI nonvolatile vector/control state across the transition
- a guest stop result makes no guarantee about guest vector state
- adding guest extended state requires a later versioned contract, preferably using an explicit XSAVE-oriented representation rather than host ABI structs

## 5. Synthetic initial context

A helper constructs the first controlled context from `GuestImage`:

- `rip = image.elf.header.entry`
- `rsp = image.initial_stack.rsp`
- all GPRs other than RSP/RIP initialize to zero
- `rbp = 0`
- `rdx = 0`
- `fs_base = 0`
- `gs_base = 0`
- RFLAGS has architectural reserved bit 1 set and DF clear

This is a deterministic Astraea synthetic profile.

It is not asserted to match PS5 retail process entry.

## 6. Execution-memory preparation

M2 v0 prepares only Astraea-owned synthetic images that satisfy a restricted direct-map profile.

### 6.1 Identity mapping

Every guest byte made available to native instructions is mapped at the same numeric virtual address in the host process.

No guest integer address is silently rebased.

### 6.2 No replacement

The backend must never overwrite an existing host mapping to obtain a guest address.

Linux must use a no-replace strategy where supported.

Windows exact-address reservation/allocation must fail if the requested address is unavailable.

Failure category: `guest_address_unavailable`.

### 6.3 Host page planning

The backend rounds guest mapping coverage to host page boundaries using checked arithmetic.

The plan preserves guest byte ranges separately from host-page coverage.

Host-page overlap caused only by rounding is resolved deterministically from all guest mappings touching that host page.

### 6.4 Population

Mapped pages are initially writable and non-executable only for controlled population.

File-backed bytes and zero-fill content come from the validated M1 image/mapping model.

Synthetic stack bytes are populated into its mapped guest range separately.

### 6.5 Final protection

After population:

- executable code pages become RX
- writable data/stack pages become RW
- read-only data becomes R
- no page remains writable+executable

If a single host page needs both W and X semantics in v0, preparation fails with `mixed_write_execute_page`.

### 6.6 Instruction cache

Backends invoke the host-required instruction-cache synchronization after code population/protection changes.

## 7. TLS boundary

M2 v0 native-entry acceptance does not require guest TLS.

If `GuestImage.tls` is present and the execution request requires installing it, the v0 backend returns `tls_runtime_layout_unsupported`.

A first synthetic execution probe may omit `PT_TLS`.

This avoids inventing FS-base/TCB policy before provenance exists.

## 8. Execution-thread ownership

One execution operation owns one dedicated OS thread for the duration of guest entry.

The thread has:

- one active backend frame
- one saved host context
- one guest context
- one fault-capture record
- host-specific recovery state

Nested guest execution on the same thread is rejected in v0.

Concurrent execution on different dedicated threads is not an M2 v0 requirement.

## 9. Host-state preservation

The transition stub must assume guest code may clobber all guest-visible registers.

It therefore preserves every host state component required for returning safely to its C++ caller.

At minimum this includes:

- host stack pointer / continuation
- host ABI nonvolatile GPRs
- host-required nonvolatile vector/control state
- signal/exception recovery bookkeeping

Host ABI requirements differ between System V AMD64 and Windows x64; that difference remains inside the adapter.

## 10. Entering guest code

The low-level transition routine receives:

- pointer/reference to portable guest context
- prepared execution-memory metadata
- recovery/stop frame

It loads the guest context only after recovery state is fully armed.

The final operation transfers control to guest RIP on guest RSP.

Ordinary C++ code must never run while RSP refers to the guest stack unless it is a deliberately designed no-throw transition/recovery thunk.

## 11. Normal host return is not a guest ABI

Guest code must not return with `ret` into an ordinary host C++ call frame.

The backend does not place a raw host return address on guest RSP.

M2 stop/exit paths use a defined gate/fault mechanism.

Issue #3 defines HLE call and synthetic exit semantics above this mechanism.

## 12. Execution stop representation

Portable result:

```text
ExecutionStop
    reason
    context
    optional fault
    optional gate metadata
```

Initial reasons:

- `host_gate`
- `guest_fault`
- `backend_error`

Issue #3 may refine host-gate payloads into HLE/exit events without changing the saved CPU context contract.

## 13. Normalized GuestFault

```text
GuestFault
    kind
    instruction_pointer
    stack_pointer
    optional fault_address
    host_code
```

Initial normalized kinds:

- access_violation
- illegal_instruction
- arithmetic
- breakpoint_or_trap
- unknown

The original host exception/signal code is preserved diagnostically.

Do not reinterpret every trap as HLE; issue #3 defines which gate encodings are intentional.

## 14. Linux x86-64 recovery adapter

Linux uses:

- dedicated execution thread
- alternate signal stack
- `sigaction` with `SA_SIGINFO | SA_ONSTACK`
- narrowly scoped handling for guest execution

Candidate signals:

- SIGSEGV
- SIGBUS
- SIGILL
- SIGFPE
- SIGTRAP when the accepted gate design requires it

Handler rules:

- confirm thread-local active execution frame
- confirm saved RIP is inside registered guest/gate execution ranges
- copy only fixed-size required state
- no allocation
- no locks
- no logging
- no C++ exceptions
- unrelated faults delegate/chain to the previous disposition

Recovery must return to a known host stack/context; no C++ unwinding through guest frames.

The implementation may redirect the saved `ucontext_t` to a recovery thunk or use another demonstrably safe mechanism, but that choice must be tested independently.

## 15. Windows x86-64 recovery adapter

Windows consumes processor exception context through platform exception facilities and normalizes it to the same portable structures.

Requirements:

- scoped to the Astraea execution thread/frame
- access to the x64 `CONTEXT` at the fault
- unrelated process exceptions are not swallowed
- recovery lands on known host-owned state
- no C++ unwinding through guest frames
- any vectored handler, if used, must chain correctly and be installed/removed with explicit lifetime

The final choice between a narrow SEH wrapper and a vectored mechanism is an implementation issue, not part of the portable interface.

## 16. Fault ownership test

A backend may claim a host fault only when:

1. current thread has an active Astraea execution frame
2. saved RIP belongs to a registered guest executable range or accepted gate range

A data fault address alone is insufficient; host code can legitimately fault while manipulating guest mappings.

This rule prevents Astraea from accidentally hiding emulator bugs as guest faults.

## 17. Backend lifecycle

Conceptually:

```text
prepare(image, profile) -> PreparedExecution
enter(prepared, context) -> ExecutionStop
destroy(prepared)
```

Preparation and entry are separate so:

- memory errors occur before register transition
- protection plans are inspectable/testable
- repeated synthetic entry can later be supported deliberately
- traces can record the exact plan used

`PreparedExecution` is move-only and owns its host mappings.

Destruction unmaps only resources it created.

## 18. Failure model

Stable backend errors should include:

- backend_unavailable
- unsupported_host_architecture
- guest_address_unavailable
- host_page_arithmetic_overflow
- host_mapping_failure
- host_protection_failure
- instruction_cache_sync_failure
- mixed_write_execute_page
- stack_mapping_failure
- entry_point_unmapped
- entry_point_not_executable
- stack_pointer_unmapped
- tls_runtime_layout_unsupported
- nested_execution_unsupported
- recovery_setup_failure
- invalid_guest_context
- internal_transition_failure

Errors preserve host OS error codes diagnostically but do not expose them as the portable semantic category.

## 19. M2 v0 security restrictions

Native execution is enabled only for fixtures explicitly marked trusted/synthetic.

The API must not accept an arbitrary filename and immediately jump into it.

Before retail/untrusted native execution is considered, Astraea needs a separate isolation threat model covering at least:

- process separation
- syscall escape
- infinite loops/resource control
- host address probing
- executable host API access
- signal/exception abuse
- self-modifying code
- JIT spraying-like behavior

## 20. macOS / Apple Silicon development

Portable execution types and tests continue to run natively on arm64 macOS.

M2 v0 does not provide an arm64 native guest backend.

An x86_64 Astraea build under Rosetta may be used for controlled developer experiments because Apple documents translation of entire x86_64 processes and support for JIT-style workloads.

However:

- Rosetta is not the semantic oracle
- Linux/Windows x86-64 remain required backend validation hosts
- Rosetta-specific behavior must not leak into portable interfaces

## 21. Verification hooks

The backend exposes structured events at transition boundaries:

- preparation complete
- guest entry
- host gate
- fault
- guest stop
- backend teardown

M2 does not yet define the full M3 trace schema.

The hook must be allocation-safe relative to fault contexts: fault handlers record fixed data first; ordinary tracing occurs only after control is safely back on the host stack.

## 22. Initial implementation sequence

After this contract is accepted:

1. portable `GuestCpuContext`, fault/stop/error types
2. pure mapping/protection planner tests
3. Linux x86-64 exact-address synthetic mapper
4. Windows x86-64 exact-address synthetic mapper
5. host-state save/restore transition stub per host ABI
6. fault recovery tests using deliberately faulting owned code
7. issue #3 HLE/exit gate contract
8. `probe_hello.elf` end-to-end

No broad PS5 API work starts before the synthetic transition is mechanically reliable.

## 23. Acceptance tests for the transition implementation

Portable:

- deterministic initial CPU context
- complete GPR round trip
- DF normalized/validated
- rejected unsupported TLS runtime request
- page-plan overflow cases
- W^X rejection
- unavailable exact address
- stack/entry validation

Linux x64:

- owned synthetic code enters and returns through controlled gate
- guest clobbers all volatile/nonvolatile GPRs without corrupting host
- deliberate unmapped read becomes normalized guest fault
- deliberate illegal instruction becomes normalized guest fault
- unrelated host fault is not consumed
- repeated enter/teardown does not leak mappings/handlers

Windows x64:

- equivalent transition/context/fault cases using the Windows adapter

macOS ARM64:

- portable contract builds/tests
- native backend reports unavailable

## 24. Decision summary

> Astraea M2 begins with trusted synthetic native x86-64 execution on Linux and Windows. Guest memory is identity-mapped without replacing host mappings, populated under non-executable permissions, then protected under strict W^X. A portable integer CPU context is separated from host signal/SEH structures. Fault recovery is scoped to a dedicated execution frame, preserves host state, and never unwinds C++ through guest frames. HLE/exit gate semantics are layered separately in issue #3.
