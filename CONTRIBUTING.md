# Contributing to Astraea

Astraea is a verification-first compatibility research project. Contributions are expected to be small, testable, and provenance-aware.

## Before coding

Read:
- `README.md`
- `docs/PROJECT_PLAN.md`
- `docs/CLEAN_ROOM.md`
- relevant ADRs/specifications
- the issue you are implementing

Do not begin a broad subsystem rewrite without an accepted ADR.

## Branches

Use a short scoped prefix:
- `feat/`
- `fix/`
- `research/`
- `test/`
- `docs/`
- `bootstrap/`

Examples:
- `feat/elf64-program-headers`
- `research/mutex-timeout-semantics`

## Pull requests

A PR should:
- solve one bounded problem
- reference its issue
- explain the evidence/specification it implements
- include tests for success and failure paths
- identify platform-specific behavior
- mention provenance/license implications
- avoid unrelated cleanup

## AI-assisted contributions

AI agents are implementation/review tools, not architectural authorities.

An AI-authored change must satisfy the same evidence, testing, provenance, and review requirements as any other contribution. Do not merge generated code merely because it builds.

Agent tasks should explicitly state:
- owned files
- prohibited files/scope
- specification/evidence
- acceptance tests
- target platforms

## Commit hygiene

Prefer focused commits with imperative summaries. Do not commit build artifacts, proprietary research material, firmware, keys, retail game content, or temporary dumps.

## Coding style

- C++23
- format with the repository `.clang-format`
- keep warnings clean
- prefer explicit ownership and narrow interfaces
- avoid undefined behavior and unchecked integer/pointer arithmetic
- treat binary input as untrusted

## Tests

Run the appropriate preset before requesting review.

On macOS:

```sh
bash scripts/bootstrap-macos.sh
cmake --preset macos-dev
cmake --build --preset macos-dev
ctest --preset macos-dev
```

Architecture-specific tests may additionally require x86-64 CI.

## Provenance

If the change encodes PS5-specific behavior, document why we believe that behavior is correct. If evidence conflicts or is incomplete, say so rather than converting uncertainty into an undocumented assumption.
