# Project Status

**Repository:** `astraea-emu/astraea`  
**Merged gate frontier after the current C0 stack:** V0/V1/V2/V3 complete;
Linux x86-64 supervised retail diagnostic admission established.  
**Compatibility claim:** none. Astraea does not currently claim that a retail
PS5 title boots, renders, reaches a menu, or is playable.

This file is intentionally a concise merged-frontier handoff. Git history,
closed issues, and merged PRs preserve the detailed milestone chronology.

## Verified merged capabilities

### Loader / CPU / HLE

- Validated PS5/SCE `GuestImage` parsing and typed memory mappings.
- Evidence-scoped dynamic metadata, symbols, relocations, exact import identity,
  and controlled HLE gate application for owned fixtures.
- Guarded native x86-64 execution for trusted Astraea-owned probes on Linux and
  Windows with normalized stop/fault state and W^X mapping policy.

### Graphics V0-V3

- **V0 complete:** AGC shader-container ingestion -> bounded RDNA2 -> semantic
  Shader IR with provenance.
- **V1 complete:** owned guest execution reaches real shader-creation HLE and
  publishes persistent stage-aware created-shader identity.
- **V2 complete:** supported semantic Shader IR lowers through the
  workload-driven compiler/value layer to deterministic Vulkan-valid SPIR-V.
- **V3 complete for the selected owned raster proof:** submitted PM4/register
  state, guest GPU allocation/image/surface identity, generated vertex/pixel
  SPIR-V, and a real Vulkan graphics pipeline produce deterministic 4x4 RGBA8
  readback.
- WRITE_DATA guest-buffer resolution and Vulkan transfer/readback are separately
  verified.
- The guest GPU allocation, image view, physical surface layout, and Vulkan
  materialization remain separate identities under ADR 0009.

### C0 retail diagnostic boundary

The Linux x86-64 C0 admission path establishes:

- separate controller and worker processes with deterministic teardown;
- bounded little-endian typed wire frames;
- typed syscall request/result, fault, diagnostic, STOP, and TERMINATE events;
- finite wall-clock/kernel CPU, address-space, descriptor, core-dump, and
  file-growth limits;
- minimal inherited descriptor authority;
- Linux pre-kernel guest-syscall containment using instruction-pointer-scoped
  seccomp/SIGSYS plus ordinary-code opcode/RIP verification;
- a verified synthetic syscall round trip that resumes after the trapped
  boundary;
- sealed anonymous artifact handoff: the controller sees the user host path;
  the worker receives only immutable bytes and closes the artifact descriptor
  before RUN;
- deterministic PS5/SCE retail preflight;
- production `astraea diagnose <artifact>` output with typed
  `boundary=` / `stage=` fields and distinguishable controller input
  failures.

The production diagnostic deliberately stops an otherwise-ready PS5/SCE image
at `unsupported_initial_process_abi`. Astraea does not substitute its
synthetic test stack for an unverified PS5 process-entry contract.

Windows retains the trusted owned-probe worker/native-execution support but is
not currently an admitted retail-diagnostic host.

## Active evidence blockers

### PS5 initial-process ABI

This is the first load-bearing blocker on the retail native-entry path.

Before executing arbitrary retail instructions, establish the PS5 initial
process contract needed by the selected executable profile: initial stack/aux
state, register contract, TLS/process metadata, and any required runtime/module
bootstrap. Do not infer the answer from Linux/FreeBSD/PS4 precedent without
PS5 evidence.

### `sceAgcLinkShaders` tail records — issue #191

The verified guest-visible LinkShaders ceiling remains the measured output from
#189. CX[32] and UC[0..2] remain unmeasured for the selected profile. The
reference-hardware observation contract in #193 remains the route to promotion.

This blocker is independent of V3 completion: Astraea reached the owned V3
raster result without laundering candidate LinkShaders tail values into
verified behavior.

## Next action

1. Merge/verify the production Linux retail-diagnostic stack.
2. Run `astraea diagnose <lawfully obtained PS5 executable>` on Linux x86-64
   and preserve only the typed diagnostic result/provenance—not retail bytes.
3. Use the returned first boundary to choose exactly one next dependency.
4. If the boundary is `unsupported_initial_process_abi`, open a bounded
   evidence task for that process-entry contract before enabling retail native
   entry.
5. Continue the same first-unsupported loop for module dependencies,
   relocations, TLS, HLE/syscalls, GPU state, and presentation as each becomes
   the earliest real blocker.
6. Resolve #191 only with sufficient evidence; do not invent LinkShaders tail
   values to accelerate compatibility.

## Non-blocking hardening after first diagnostic

The current C0 boundary is sufficient for the first bounded diagnostic, but
Linux defense in depth can improve without changing guest semantics:

- evaluate pidfd-based worker lifetime/signalling to reduce PID-identity races;
- evaluate Landlock for additional ambient filesystem/network/IPC restriction
  where the running kernel supports the required ABI;
- keep these host-containment layers separate from the portable guest syscall
  contract.

These are hardening tasks, not reasons to delay the first diagnostic.

## CI merge gate

Every authoritative code head must pass:

1. Linux x64
2. Windows x64
3. macOS ARM64
4. Linux ASan + UBSan
5. Linux Clang fuzz smoke

## Clean-room boundary

Do not commit Sony firmware, keys, proprietary SDK material, decrypted retail
game assets, copyrighted PS5 executables, proprietary system modules,
DRM-bypass material, or unrelated employer/proprietary material.

PS5-specific assumptions require documented public evidence, controlled lawful
observation, or an explicitly labeled provisional hypothesis under ADR 0008.
