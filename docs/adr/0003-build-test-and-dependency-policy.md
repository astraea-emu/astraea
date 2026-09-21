# ADR 0003: Build, test, and dependency policy

**Status:** Accepted  
**Date:** 2026-09-20

## Context

A long-lived emulator requires reproducible cross-platform builds, fast local iteration, strong malformed-input testing, and controlled dependency growth.

## Decision

- Language baseline: C++23.
- Build system: CMake with checked-in presets.
- Preferred local generator on macOS/Linux: Ninja.
- Unit test framework: Catch2 v3, pinned to an explicit release.
- Test registration: CTest + Catch2 discovery.
- CI hosts: Ubuntu x86-64, Windows x86-64, macOS ARM64.
- Warnings are enabled broadly; CI may promote Astraea warnings to errors.
- Sanitizer jobs begin with Linux Clang/GCC where supported.
- Parser-facing code will gain fuzz targets as soon as substantive parsers exist.
- Dependencies must be pinned and justified; platform behavior must not hide inside opaque dependencies.

## Consequences

### Positive
- One build graph across supported hosts.
- Strong ecosystem support for C++ testing and tooling.
- Dependency versions are reviewable.

### Negative / constraints
- FetchContent-based test dependencies require network access on a clean configure unless cached.
- Compiler differences require carefully scoped warning flags.

## Alternatives considered

### IDE-specific project files
Rejected because they fragment the source of truth.

### A custom unit-test framework
Rejected because maintaining test infrastructure is not Astraea's differentiator.

## Evidence / references

Catch2 v3 and CMake/CTest are established cross-platform C++ tooling. Dependency revisions are pinned in the build configuration.

## Revisit triggers

Revisit if build performance, offline reproducibility, package management, or dependency security requirements materially change.
