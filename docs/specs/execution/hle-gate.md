# Synthetic Host Gate and HLE Dispatch ABI — M2

**Status:** Proposed  
**Issue:** #3  
**Scope:** Astraea-owned synthetic guest-to-host calls before PS5-specific import binding

## 1. Purpose

M2 needs a deterministic way for trusted synthetic x86-64 guest code to request a host service and then either resume guest execution or terminate the synthetic run.

This contract defines:

- how a guest intentionally stops at a host gate
- how a gate is identified without decoding arbitrary guest data in a signal/exception handler
- how integer arguments/results cross the boundary
- how the host registry dispatches handlers
- how a normal HLE call resumes as though a guest function returned
- how a synthetic exit operation terminates execution
- where future PS5 import/NID binding attaches

It does **not** define Sony/PS5 import identity.

## 2. Gate instruction choice

Each Astraea synthetic gate stub begins with:

```text
UD2    ; 0F 0B
```

Intel defines UD2 as an instruction that always raises the invalid-opcode exception and whose saved instruction pointer references the UD2 instruction itself.

That makes it suitable for an intentional synthetic stop when—and only when—the faulting RIP is an address pre-registered as an Astraea gate stub.

An arbitrary SIGILL / illegal-instruction exception is **not** an HLE call.

## 3. Dedicated gate region

The execution backend owns one dedicated guest RX range:

```text
GateRegion
    base: GuestAddress
    stride: uint64
    slot_count: uint32
```

M2 v0 uses:

```text
stride = 16 bytes
```

Every valid gate RIP is:

```text
base + slot * stride
```

for:

```text
slot < slot_count
```

and the first two bytes at the slot are the backend-generated UD2 instruction.

### Fault-handler recognition

The fault adapter recognizes an intentional gate **arithmetically**:

1. active Astraea execution frame exists on this thread
2. host exception is the platform's invalid-opcode condition
3. saved RIP lies in the registered gate region
4. `(RIP - base) % stride == 0`
5. derived slot is less than `slot_count`

The signal/exception handler does not:

- allocate
- lock
- log
- consult a hash map
- resolve a symbol
- run an HLE callback
- parse variable-length guest data

It records a fixed-size gate stop and transfers control to the normal host recovery path.

## 4. Gate page ownership

Gate pages are backend-owned execution infrastructure, not PT_LOAD contents.

They:

- are identity-mapped at explicit guest addresses
- are never writable while guest code executes
- contain only backend-generated gate stubs/padding
- are included in fault-ownership / executable-range checks
- are torn down with the prepared execution object

Gate placement must not overlap:

- any GuestImage mapping
- synthetic stack storage
- another backend-owned runtime range

## 5. Guest call shape

Synthetic guest code invokes a service using an ordinary x86-64 `call` to its assigned gate stub.

Therefore at the gate fault:

- RIP points to the UD2 gate stub
- RSP points to the guest return address pushed by `call`

The host must **not** put a host C++ return address on guest RSP.

## 6. Synthetic argument ABI

Astraea Synthetic Gate ABI v0 uses a SysV-shaped integer register order purely as an internal convention:

```text
arg0 -> RDI
arg1 -> RSI
arg2 -> RDX
arg3 -> RCX
arg4 -> R8
arg5 -> R9
result -> RAX
```

This choice is independent of the host OS ABI.

A Windows host backend interprets guest arguments from the same guest registers.

M2 v0 supports only:

- integer/pointer-shaped arguments
- up to six register arguments
- one 64-bit integer/pointer-shaped RAX result

Not supported yet:

- stack arguments
- floating-point/SIMD arguments or returns
- structures by value
- variadic calling conventions
- vector registers
- guest errno/TLS conventions

This is an Astraea synthetic ABI and is not claimed to be the PS5 HLE ABI.

## 7. Stable HLE function identity

Host handlers use an Astraea-internal identity independent of gate slot and PS5 symbol identity.

Conceptually:

```text
HleFunctionId = uint32
```

A descriptor contains:

```text
HleFunctionDescriptor
    id
    canonical_name
    argument_count   // 0..6 in v0
    behavior flags
```

Requirements:

- ID 0 is reserved as invalid
- IDs are unique in one registry
- canonical names are unique in one registry
- duplicate registration fails deterministically
- gate slot assignment is separate from function identity

Future PS5 import resolution may map:

```text
(module identity, library identity, NID)
        ->
HleFunctionId
        ->
gate slot / generated thunk
```

without changing the synthetic dispatch ABI.

## 8. Gate binding table

Preparation constructs an immutable binding table:

```text
GateBinding
    slot
    function_id
```

Properties:

- each slot has at most one function
- each bound function exists in the registry
- slot 0 may be reserved for backend diagnostics if desired; this must be explicit
- bindings become immutable before guest entry
- the fault handler stores only the derived slot, not a handler pointer

Ordinary host code resolves `slot -> function_id -> handler` after recovery.

## 9. HLE call snapshot

After the host is safely back on its normal stack, dispatch materializes:

```text
HleCall
    function_id
    gate_slot
    guest_rip
    guest_rsp
    arguments[6]
```

Arguments are copied from the saved `GuestCpuContext`.

Unused argument positions are still deterministic raw register values; the descriptor's `argument_count` determines which are semantically passed to the handler.

## 10. Guest memory access

Handlers must not cast arbitrary guest integers to host pointers.

They receive a bounded guest-memory access interface owned by the prepared execution layer.

Initial operations should be capability-shaped, for example:

```text
read(GuestAddress, span<byte>)
write(GuestAddress, span<const byte>)
read_c_string(GuestAddress, max_bytes)
```

Every access validates:

- guest range arithmetic
- prepared mapping coverage
- required R/W permission
- host-size bounds

A handler failure caused by invalid guest memory is returned as a typed HLE/guest-access failure, not a host segfault.

M2's first `test_write` handler requires read access only.

## 11. Handler interface

Conceptually:

```text
HleHandler(
    HleCall,
    GuestMemoryAccess&
) -> HleHandlerResult
```

Handlers run only in normal C++ host context after signal/exception recovery.

They may allocate/log according to normal host policy; those operations are never executed inside the signal/exception handler.

## 12. Handler result

M2 v0 distinguishes:

```text
resume(result_u64)
exit(exit_code_u64)
failure(HleError)
```

### Resume

For `resume(value)`:

1. validate guest RSP can read one 64-bit return address
2. read return RIP from guest stack, little-endian
3. checked `RSP += 8`
4. validate return RIP is in a prepared guest executable range
5. set:
   - `RAX = value`
   - `RIP = return_rip`
   - `RSP = old_rsp + 8`
6. resume via the native backend

This emulates the architectural effect of a normal function return from the synthetic gate.

The UD2 instruction itself is never re-executed after a successful resume.

### Exit

For `exit(code)`:

- no guest return-address pop is required
- the execution session terminates normally
- top-level result records the synthetic guest exit code
- no further guest instruction executes

### Failure

Dispatch failure does not silently resume.

It returns a typed backend/HLE stop to the execution controller.

## 13. Return-address validation

A return address is valid for v0 only if it belongs to an exact prepared guest executable mapping/runtime region accepted by execution policy.

Host-page rounding alone must not make a return address executable.

This follows the same exact-guest-range principle as the portable execution planner's entry-point validation.

## 14. Registry lifecycle

The registry is configured before a prepared execution begins.

M2 v0 rules:

- no registration mutation while guest code is active
- no gate rebinding while guest code is active
- handlers have stable lifetime through the execution session
- registry lookup occurs only after recovery to the host stack

Concurrent multi-session mutation is outside v0.

## 15. Synthetic built-in services

The first end-to-end probe needs exactly two synthetic services.

### `astraea.test.write`

Suggested ID:

```text
1
```

Arguments:

```text
RDI = guest buffer address
RSI = byte count
```

Behavior:

- bounded read of exactly `byte_count` guest bytes
- host captures/emits those bytes to the synthetic probe result/trace
- returns number of bytes consumed in RAX
- a host-size or guest-range violation is a typed HLE failure

No implicit NUL termination is required.

### `astraea.test.exit`

Suggested ID:

```text
2
```

Arguments:

```text
RDI = exit code
```

Behavior:

- terminates synthetic execution with that code
- does not resume guest code

These services are test infrastructure, not PS5 APIs.

## 16. Unsupported / unbound gates

A recognized gate slot with no valid binding produces:

```text
unbound_gate
```

A binding referencing a missing function must be rejected during preparation and therefore should not occur at runtime.

An illegal instruction outside the gate region remains:

```text
GuestFaultKind::illegal_instruction
```

It must never be transformed into an unsupported HLE call.

## 17. Errors

Stable HLE/gate categories should include at least:

- duplicate_function_id
- duplicate_function_name
- invalid_function_id
- invalid_argument_count
- duplicate_gate_binding
- gate_slot_out_of_bounds
- unknown_function
- unbound_gate
- guest_stack_unreadable
- guest_return_address_unreadable
- guest_return_address_not_executable
- guest_stack_pointer_overflow
- guest_memory_unmapped
- guest_memory_permission_denied
- guest_memory_range_overflow
- host_size_unrepresentable
- handler_failure

Errors preserve:

- function ID when available
- gate slot when available
- guest address when applicable

## 18. Trace hook boundary

M2 exposes lightweight host-side events after safe recovery:

```text
gate_stop(slot, rip, rsp)
hle_begin(function_id, slot, args)
hle_resume(function_id, result, next_rip)
hle_exit(function_id, exit_code)
hle_failure(function_id, error)
```

This is not yet M3's stable trace schema.

The M2 hook should contain enough structured fields for M3 to map it into the eventual trace format.

No trace callback runs inside the signal/exception handler.

## 19. Linux/Windows fault integration

### Linux

A SIGILL/#UD stop becomes `host_gate` only when RIP matches the registered gate-region slot rule.

Any other SIGILL becomes `illegal_instruction`.

### Windows

An illegal-instruction exception becomes `host_gate` only under the same guest RIP/gate-region rule.

Guest-side semantics therefore remain identical across host OSes.

## 20. Gate-region bytes

M2 v0 gate slot layout:

```text
offset +0: 0F 0B      UD2
offset +2..15: fixed backend padding
```

The padding pattern is backend-generated and deterministic.

The host derives slot identity from RIP; it does not rely on hidden immediate bytes after UD2.

This avoids requiring the async fault path to read/decode guest memory.

## 21. Security properties

- only pre-registered gate addresses classify #UD as HLE
- no handler executes in signal/exception context
- no arbitrary host function pointer comes from guest memory
- guest pointers pass through bounded guest-memory accessors
- return RIP is validated against exact executable guest ranges
- registry/binding tables are immutable during guest execution
- synthetic v0 gates remain unavailable to arbitrary retail code execution policy

## 22. PS5 boundary

This spec intentionally does **not** define:

- PS5 NIDs
- SCE module/library IDs
- retail import-stub encoding
- PS5 syscall ABI
- PS5 kernel error conventions
- PS5 argument ABI deviations, if any
- system-module versus HLE binding precedence

Those remain behind #8 and future probe-backed evidence.

The eventual PS5 resolver should bind platform identities **to** `HleFunctionId`; it should not redefine the gate execution protocol.

## 23. M2 acceptance sequence

After this contract is accepted:

1. implement registry + binding validation
2. implement gate-region metadata/generation
3. integrate exact gate recognition into Linux #4
4. implement bounded guest-memory accessor
5. implement `astraea.test.write`
6. implement `astraea.test.exit`
7. create owned synthetic `probe_hello.elf`
8. execute:
   - guest enters at ELF RIP
   - guest calls write gate
   - host captures `Hello from guest`
   - RAX receives byte count
   - guest calls exit gate with 42
   - execution returns exit code 42
   - structured transition/HLE events are emitted
   - PASS
9. reproduce equivalent behavior on Windows #5

## 24. Evidence

- Intel UD2 semantics: Intel® 64 and IA-32 Architectures Software Developer's Manual
- x86-64 register conventions: x86-64 psABI

The SysV-shaped synthetic argument order is an Astraea design choice, not evidence of PS5 behavior.

## 25. Decision summary

> M2 synthetic HLE uses call-style jumps into a dedicated backend-owned RX gate region. A gate slot is recognized solely from the faulting RIP and fixed region geometry; UD2 provides the intentional invalid-opcode stop. Dispatch occurs only after recovery to a normal host stack. Synthetic integer arguments use a host-independent SysV-shaped register order, results return in RAX, resume emulates a guest RET after validating the guest return address, and exit terminates the synthetic run. PS5 import identities bind to this layer later rather than being invented here.
