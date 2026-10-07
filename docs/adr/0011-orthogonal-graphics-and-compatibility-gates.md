# ADR 0011: Track graphics integration and retail compatibility as orthogonal gates

**Status:** Accepted  
**Date:** 2026-09-24

## Context

Astraea originally used V0-V5 as the primary vertical integration ladder.
That model successfully drove the first graphics path from AGC shader ingestion
through a submitted owned PM4 workload and deterministic Vulkan readback.

By the time V3 completed, the CPU/retail path had also advanced independently:

- supervised controller/worker execution;
- typed worker protocol;
- Linux and Windows owned native-execution proofs;
- Linux pre-kernel guest-syscall containment;
- typed guest faults;
- resource ceilings;
- sealed retail artifact handoff;
- production Linux retail preflight/diagnostic entry.

A single linear gate list became misleading. V3 completion does not mean a
commercial title should boot, while C0 completion does not imply that the PS5
process-entry ABI is known.

"Test a game" also has several materially different meanings: parsing a title,
executing its first instruction, reaching sustained initialization, producing a
headless frame, presenting a menu, and being playable are not equivalent
milestones.

## Decision

Astraea tracks two orthogonal integration ladders.

### Graphics / rendering technology

```text
V0 shader ingestion
V1 guest-created shader identity
V2 validated host shader module
V3 submitted guest GPU workload -> deterministic Vulkan result
V4 controlled PS5 differential when evidence requires it
V5 VideoOut / host presentation
```

V0-V3 are complete for their bounded owned workloads.

### Retail compatibility

```text
C0 supervised production retail diagnostic
C1 evidenced PS5 process-entry ABI / first retail instruction
C2 runtime/bootstrap closure
C3 deterministic title boot / sustained initialization
C4 first real-title headless GPU/frame evidence
C5 visible presentation / menu
C6 in-game / playable / accuracy progression
```

C0 is complete on Linux x86-64. C1 is active.

A title category is a reproducible integration observation, not evidence that
every underlying subsystem is semantically complete.

## Direct-title-first strategy

Full firmware/VSH boot is not a prerequisite for the compatibility ladder.

Astraea prefers direct-title diagnostics because they expose the next missing
loader/runtime/HLE/GPU dependency sooner and with less unrelated system
surface. Firmware/VSH work may become a separate track if a concrete selected
dependency justifies it.

## Dependency selection

After a merge, choose the first missing dependency on the active gate.

Expected post-C1 classes include module/runtime linking, TLS/thread/process
state, HLE services, guest object/handle models, broader shader/resource
semantics, memory page tracking, resource/pipeline caches, synchronization, and
presentation.

These are not a speculative implementation checklist. They become work only
when the next selected workload demonstrates the need.

## Compatibility reporting

When real title testing begins, normalized reports should distinguish at least:

- diagnostic;
- native-entry;
- boot;
- first-frame/headless-GPU;
- menu;
- in-game;
- playable;
- accurate.

Reports should include Astraea commit, host configuration, artifact/title
identity by lawful metadata/digest, first unsupported boundary, and normalized
log/trace identifiers. They must not contain copyrighted retail bytes, keys,
firmware, or decrypted system modules.

## Consequences

### Positive

- Graphics progress cannot be mistaken for game compatibility.
- Retail safety/boot progress cannot be mistaken for GPU completeness.
- The first legally owned title can drive dependency selection without title
  hacks.
- "First game test" has an explicit place: C0 diagnostic is complete; first
  native retail instruction belongs to C1.
- Future compatibility claims become reproducible categories instead of vague
  screenshots or one-off successes.

### Constraints

- The repository maintains two gate axes instead of one.
- A later C-gate may require work in an earlier-completed V-gate's subsystem;
  bounded gate completion never means global subsystem completeness.
- Competitor feature breadth is architectural inspiration, not justification
  to pre-implement unused APIs.

## Alternatives considered

### Extend V0-V5 with title boot milestones

Rejected. It conflates GPU technology and whole-title execution and would make
V3/V5 terminology ambiguous.

### Make full firmware/VSH boot the next milestone

Rejected as the default critical path. It introduces a much larger service
surface before a selected title can expose the first actionable dependency.

### Use a single percentage for "emulator completion"

Rejected. Unknown workloads and undocumented platform behavior make a single
percentage misleading.

## Related

- ADR 0006 — dependency-driven vertical integration
- ADR 0007 — separate semantic Shader IR and compiler/value IR
- ADR 0008 — verified/provisional research tracks
- ADR 0009 — guest GPU image/surface identity
- ADR 0010 — supervised retail execution
- `docs/PROJECT_PLAN.md`
- `docs/STATUS.md`
- `docs/research/architecture_review_2026-09-24.md`
