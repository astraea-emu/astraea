# Astraea

A verification-first PlayStation 5 compatibility research and emulation
project.

> **Status:** V0 shader ingestion, V1 guest-created shader identity, V2
> validated SPIR-V, and the bounded V3 submitted-state/resources -> generated
> SPIR-V -> Vulkan raster proof are complete. Astraea also has a supervised
> Linux x86-64 retail-diagnostic boundary: user-selected artifact bytes cross
> into a separate worker only through a sealed immutable handoff, guest raw
> syscalls are contained before host-kernel execution, and unsupported
> behavior is returned as typed diagnostics/faults. The current production
> diagnostic intentionally stops an otherwise-ready PS5/SCE image at the
> unsupported initial-process ABI boundary. Astraea does **not** claim that
> retail PlayStation 5 games boot, render, or are playable.

## Principles

- Clean-room implementation: no proprietary Sony source code, firmware, keys,
  SDK files, proprietary system modules, or copyrighted retail assets in this
  repository.
- Evidence before emulation: document public or controlled behavior before
  encoding PS5-specific assumptions.
- Guest semantics before host mapping: SCE/AGC/RDNA2 behavior remains separate
  from Vulkan and other host APIs.
- Native x86-64 execution where host architecture permits it; portable
  subsystems remain host-independent.
- Verification-first development: structured traces, differential tests,
  regression localization, fuzzing, sanitizers, and reproducible experiments.
- Dependency-driven vertical integration: build the smallest real dependency
  that advances the next end-to-end gate instead of maximizing isolated API or
  opcode coverage.
- Small reviewed changes with explicit scope and acceptance criteria.
- GitHub is the durable source of truth for architecture, status, decisions,
  evidence, and handoffs.

## Architecture

Astraea keeps guest-domain behavior separate from host implementation.

1. **Guest image / CPU path** — SCE ELF/module parsing, mappings, relocations,
   exact import identity, native x86-64 execution, and HLE platform services.
2. **PS5 GPU frontend** — AGC shader containers/objects, command buffers,
   register/state, guest resources, synchronization, and presentation state.
3. **Shader semantics/compiler** — AMD-documented RDNA2 decoding, semantic
   Shader IR and CFG, semantic-oracle execution for verified subsets, a
   workload-driven compiler/value IR when required, then SPIR-V lowering.
4. **Host GPU backend** — Vulkan resource/pipeline/synchronization
   materialization from the guest GPU model. Vulkan is not the guest API.
5. **Retail supervisor** — a separate worker process, bounded typed protocol,
   kernel/resource containment, sealed artifact authority, and deterministic
   first-unsupported reporting for Linux x86-64 diagnostics.
6. **Astraea Lab** — trace capture, normalization, diffing, regression
   minimization, and behavioral corpora.
7. **Astraea Probe** — controlled owned programs and experiments for isolating
   platform behavior and, when appropriate, lawful hardware comparison.

"Generic RDNA2" in Astraea means AMD-defined guest ISA semantics shared by the
hardware family. It is real emulator behavior, not placeholder data. PS5 AGC
metadata remains a separate Sony-specific frontend because the two layers are
not interchangeable.

## Development hosts

The project is designed to be developed from macOS, Linux, and Windows. Native
x86-64 guest execution targets x86-64 Linux and Windows. Apple Silicon macOS
remains a first-class development host for portable components, but it is not
treated as an x86-64 execution host.

The first graphics backend is Vulkan. SPIR-V is the first host shader IR.
Astraea's Shader IR remains the backend-independent guest-semantic/oracle
representation; a separate compiler/value IR is used when a concrete workload
requires structured control flow, values, resources, or stage I/O. See ADR
0007.

Retail diagnostic admission is currently Linux x86-64 only. Windows continues
to support the trusted owned-probe worker/runtime contracts but is not an
admitted retail-diagnostic host.

## Current integration path

Astraea uses dependency-driven vertical gates rather than a strict "finish all
HLE, then graphics" waterfall:

```text
V0  AGC container -> RDNA2 -> semantic Shader IR                 COMPLETE
 |
 v
V1  owned guest -> persistent AGC shader identity                 COMPLETE
 |
 v
V2  supported semantic Shader IR -> validated SPIR-V              COMPLETE
 |
 v
V3  submitted guest state/resources -> Vulkan -> deterministic    COMPLETE
 |
 v
C0  supervised Linux retail diagnostic admission                  ESTABLISHED
 |   current truthful stop: unsupported PS5 initial-process ABI
 v
compatibility dependency loop
 |   process ABI -> modules/relocations/TLS -> HLE/syscalls -> wider GPU state
 v
V4  controlled PS5 differential when evidence requires it
 |
 v
V5  guest flip/VideoOut -> host presentation
```

V3 completion means the selected Astraea-owned raster workload reaches a real
Vulkan graphics pipeline and deterministic readback. It does not mean broad
PS5 GPU coverage.

C0 establishment means a lawfully obtained artifact can be admitted to the
Linux diagnostic pipeline without giving retail code ambient controller
authority or allowing raw guest syscalls to become host syscalls. It does not
mean a commercial title is bootable.

Platform HLE, RDNA2 instruction coverage, resource semantics, command decoding,
process ABI, TLS, and system services are pulled into the path only when the
next diagnostic/workload requires them. Unknown Sony-specific behavior is
recorded as an evidence blocker rather than guessed.

The last merged frontier, blockers, and next dependency live in
`docs/STATUS.md`; live open GitHub PRs/issues identify any in-flight branch
or work item. This README intentionally describes durable architecture rather
than duplicating volatile branch state.

See:

- `docs/PROJECT_PLAN.md`
- `docs/STATUS.md`
- `docs/CLEAN_ROOM.md`
- `docs/adr/0006-dependency-driven-vertical-integration.md`
- `docs/adr/0010-supervised-retail-execution.md`
- `docs/research/retail_execution_supervisor.md`
- `docs/DEVELOPMENT_MACOS.md`
- `docs/CHAT_HANDOFF.md`

## Compatibility boundary

Astraea's production retail diagnostic accepts a user-selected artifact only
on Linux x86-64. The controller reads the host path, passes immutable sealed
bytes to a separate worker, applies finite resource limits, and receives typed
diagnostic/fault events. The worker does not receive the original host path and
no generic filesystem/network syscall service is enabled.

The current path does **not** synthesize a PS5 process-entry ABI merely to
execute more instructions. An otherwise-ready image stops at
`unsupported_initial_process_abi` until that contract is independently
justified. Commercial-title progress remains an integration signal, not a
correctness oracle.

## License

Astraea is licensed under the GNU General Public License v3.0 or later. See
`LICENSE`.
