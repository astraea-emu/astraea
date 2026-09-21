# Development on macOS

## Purpose

macOS is the primary control/development workstation for Astraea. The repository must remain pleasant to build and test on a Mac without pretending that an ARM64 Mac is an x86-64 PS5 execution host.

## 1. Required baseline

Use the current supported macOS and Xcode Command Line Tools.

Initial local tools will be standardized around:

- Git
- CMake
- Ninja
- Apple Clang for the default local build
- Python 3 for project tooling
- ccache where useful
- GitHub CLI only when a local workflow needs it
- current ChatGPT desktop/Codex tooling if used for implementation

The bootstrap script verifies the documented project prerequisites rather than relying on undocumented machine state.

## 2. Architecture detection

Every setup/build script must detect:

```sh
uname -s
uname -m
```

Expected Mac architectures:
- `arm64` — Apple Silicon
- `x86_64` — Intel Mac

Architecture-sensitive code must be gated explicitly in CMake. Never silently treat ARM64 as native-x86 execution capable.

## 3. Local build roles

### Always supported locally
- documentation
- loader/parser development
- serialization
- trace/diff tooling
- HLE contracts and platform-neutral implementations
- fuzz corpus work
- unit tests that do not execute guest x86-64 directly
- shader decoder/IR logic
- static analysis where available

### x86-64-only initially
- native guest entry/exit
- direct native guest execution
- architecture-specific fault/trap handling
- x86-64 ABI transition tests

On Apple Silicon these tests are built/validated in x86-64 CI or run on dedicated x86-64 hardware.

## 4. Rosetta policy

Rosetta may be useful for developer tools, but Astraea must not depend on Rosetta as the architectural solution for arbitrary PS5 guest execution.

Any Rosetta experiment must be isolated behind an experimental flag and cannot become a correctness dependency without an ADR and repeatable tests.

## 5. Graphics on macOS

macOS has no native Vulkan implementation. Development may use Vulkan-on-Metal through MoltenVK for portability and UI/backend smoke testing.

Policy:
- Vulkan-facing abstractions must compile on macOS.
- MoltenVK results are useful for portability testing.
- Vulkan semantic/conformance conclusions are validated on native Vulkan hosts.
- GPU command/shader correctness is tested independently of host presentation wherever possible.

## 6. CI matrix

Initial required matrix:

- Ubuntu x86-64 — primary portable/core + future native execution
- Windows x86-64 — primary portable/core + future native execution
- macOS ARM64 — developer portability
- optional macOS Intel — periodic compatibility if CI cost is justified

Later:
- Linux ARM64 — portability pressure-test
- dedicated native-GPU runner — graphics validation
- sanitizer/fuzzer-specialized Linux jobs

## 7. Local directory convention

Recommended clone location:

```
~/Projects/astraea
```

Do not store:
- firmware dumps
- keys
- retail game assets
- proprietary SDK material

inside the repository.

If private local research artifacts are ever needed, they must live outside the Git tree under an explicitly ignored local path with provenance documented separately.

## 8. IDE/editor

No IDE is required by the project.

Supported workflow should work from:
- VS Code
- CLion
- Xcode as an editor
- terminal
- Codex/ChatGPT desktop tooling

Repository behavior must be driven by CMake/CTest/scripts, not IDE-specific project files.

## 9. Reproducible build interface

The target developer experience is:

```sh
git clone https://github.com/astraea-emu/astraea.git
cd astraea
bash scripts/bootstrap-macos.sh
cmake --preset macos-dev
cmake --build --preset macos-dev
ctest --preset macos-dev
```

The bootstrap script is tracked at `scripts/bootstrap-macos.sh`; changes to the documented developer workflow should keep it in sync.

## 10. Security

Treat downloaded fixtures, binaries, traces, and shader data as untrusted.

- parser inputs are bounds-checked
- risky parsers get fuzz targets
- no setup script requests broad system permissions without a documented reason
- self-hosted runners are not exposed to untrusted external PR code
