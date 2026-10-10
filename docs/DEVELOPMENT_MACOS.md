# Development on macOS

macOS is supported for portable Astraea development. Apple Silicon is not a native x86-64 guest-execution host; the Linux x86-64 supervised diagnostic and the Windows x86-64 owned-execution tests are verified on their respective hosts.

## Prerequisites

- macOS with Xcode Command Line Tools (`xcode-select -p`);
- Git, CMake 3.25 or later, Ninja and Python 3;
- a C++23-capable compiler (Apple Clang is the default local choice).

The bootstrap script checks these tools and reports the detected architecture. It does not install packages or request administrator privileges.

## Build and test

```sh
git clone https://github.com/astraea-emu/astraea.git
cd astraea
bash scripts/bootstrap-macos.sh
cmake --preset macos-dev
cmake --build --preset macos-dev
ctest --preset macos-dev
```

Build outputs live under `out/build/`. Native Linux guest execution, seccomp and platform-specific fault tests cannot be reproduced by running the ARM64 macOS preset; consult the CI results for those tests.

## Architecture and graphics

- Detect host architecture using `uname -s` and `uname -m`. Keep x86-64 guest code behind explicit build/runtime guards.
- The loader, metadata analyzers, traces, shader IR and many parser/graphics tests can be developed on Apple Silicon.
- MoltenVK can help with Vulkan portability experiments, but native-Vulkan Linux tests are the authoritative CI proof for the bounded Vulkan execution path. MoltenVK success alone is not GPU semantic conformance evidence.
- Rosetta may be used for tools and isolated research. It is not Astraea's supported route for arbitrary PS5 guest execution.

## Current CI gates

Every pull request runs the standard CI jobs:

- Linux x64 build, CTest and required software-Vulkan probes;
- Windows x64 build and CTest;
- macOS ARM64 build and CTest;
- Linux AddressSanitizer/UndefinedBehaviorSanitizer;
- Linux Clang parser fuzz smoke.

Changes affecting the pinned external PS5-format title and observer also run the separate `C1 pinned observer build proof` workflow. Neither workflow executes on a physical PS5. See `README.md` and `docs/STATUS.md` for the current compatibility boundary.

## Local files and security

An editor or IDE is optional; the authoritative build and test interfaces are CMake and CTest. VS Code, CLion and terminal workflows must not need separate project-specific settings.

Keep firmware, keys, copyrighted game assets, proprietary SDK material and private research captures **outside the Git tree**. Local inputs are untrusted: use bounded tools, preserve provenance and do not run arbitrary downloaded guest code outside the documented supervisor. The project does not require a particular AI coding assistant or editor.
