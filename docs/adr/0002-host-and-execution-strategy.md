# ADR 0002: Host and execution strategy

**Status:** Accepted  
**Date:** 2026-09-20

## Context

The PS5 CPU ISA is x86-64. The primary developer workstation is macOS and may be Apple Silicon (ARM64). Forcing every subsystem to depend on host-native x86-64 execution would make the Mac development experience poor, while pretending ARM64 is equivalent would invalidate execution testing.

## Decision

Astraea will separate portable platform modeling from architecture-specific execution.

- Portable components must build on macOS ARM64, Linux x86-64, and Windows x86-64.
- The initial fast execution path will use native x86-64 guest execution only on compatible x86-64 hosts.
- Architecture-specific transitions/fault handling live behind explicit interfaces and build gates.
- ARM64-native CPU execution/translation is a later independent design problem, not an M0/M1 requirement.
- macOS remains a first-class development host for loader, parsers, HLE contracts, traces, IRs, tooling, and portable tests.

## Consequences

### Positive
- Development remains productive on Apple Silicon.
- Native x86-64 work can be tested on CI/dedicated hosts.
- Host assumptions become explicit rather than leaking throughout the codebase.

### Negative / constraints
- Some tests cannot run locally on ARM64 Macs.
- A future ARM64 execution backend may require substantial work.

## Alternatives considered

### Depend on Rosetta
Rejected as a core architecture dependency. Rosetta can be studied experimentally but does not define Astraea's portable execution model.

### Implement an x86-64 interpreter/JIT first
Rejected for the initial critical path because compatible x86-64 hosts can execute suitable guest CPU code natively; translation may be revisited for ARM64 hosts and instrumentation.

## Evidence / references

See `docs/DEVELOPMENT_MACOS.md`.

## Revisit triggers

Revisit when ARM64 runtime support becomes a project priority or when instrumentation requirements cannot be met cleanly around native execution.
