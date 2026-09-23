# Astraea

A verification-first PlayStation 5 compatibility research and emulation
project.

> **Status:** Controlled Astraea-owned x86-64 guest execution is established on
> Linux and Windows. V0 shader ingestion, V1 guest-created shader identity, and
> the bounded V2 validated-SPIR-V proof are complete. V3 is active: Astraea has
> a mandatory headless Vulkan semantic proof, captured AGC submissions, typed
> PM4 shader-register state, and created-shader identity, but submitted guest
> state/resources do not yet execute end to end on Vulkan. Astraea does not
> currently claim retail PlayStation 5 software compatibility.

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
5. **Astraea Lab** — trace capture, normalization, diffing, regression
   minimization, and behavioral corpora.
6. **Astraea Probe** — controlled owned programs and experiments for isolating
   platform behavior and, when appropriate, lawful hardware comparison.

"Generic RDNA2" in Astraea means AMD-defined guest ISA semantics shared by the
hardware family. It is real emulator behavior, not placeholder data. PS5 AGC
metadata remains a separate Sony-specific frontend because the two layers are
not interchangeable.

## Development hosts

The project is designed to be developed from macOS, Linux, and Windows. Native
x86-64 guest execution targets x86-64 Linux and Windows first. Apple Silicon
macOS remains a first-class development host for portable components, but it
is not treated as an x86-64 execution host.

The first graphics backend target is Vulkan. SPIR-V is the first host shader
IR. Astraea's existing Shader IR remains the backend-independent
guest-semantic/oracle representation; a separate compiler/value IR is
introduced only when a concrete workload requires structured control flow,
resources, or stage I/O. See ADR 0007.

## Current integration path

Astraea now uses vertical gates rather than a strict "finish all HLE, then
graphics" waterfall:

```text
V0  AGC container -> RDNA2 -> semantic Shader IR           COMPLETE
 |
 v
V1  owned guest -> guest-domain AGC shader identity         COMPLETE
 |
 v
V2  supported semantic Shader IR -> validated SPIR-V        COMPLETE
 |
 v
V3  submitted guest state/resources -> Vulkan -> result     ACTIVE
 |   (first headless Vulkan semantic proof is complete)
 v
V4  controlled PS5 differential when evidence requires it
 |
 v
V5  guest flip/VideoOut -> host presentation
```

Platform HLE, RDNA2 instruction coverage, resource semantics, and command
decoding are pulled into this path when a gate needs them. Unknown
Sony-specific behavior is recorded as an evidence blocker rather than guessed.

The exact active branch, issue, blocker, and next action live only in
`docs/STATUS.md`; this README intentionally describes durable architecture
rather than duplicating the volatile frontier.

See:

- `docs/PROJECT_PLAN.md`
- `docs/STATUS.md`
- `docs/adr/0006-dependency-driven-vertical-integration.md`
- `docs/adr/0007-separate-shader-semantic-and-compiler-ir.md`
- `docs/DEVELOPMENT_MACOS.md`
- `docs/CHAT_HANDOFF.md`

## Compatibility boundary

Astraea currently executes only trusted Astraea-owned synthetic probes through
the native guest path. Commercial-title compatibility is an integration
outcome, not the correctness oracle, and arbitrary retail guest execution is
not enabled.

## License

Astraea is licensed under the GNU General Public License v3.0 or later. See
`LICENSE`.
