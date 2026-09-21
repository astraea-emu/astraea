#!/usr/bin/env bash
set -euo pipefail

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "error: bootstrap-macos.sh must be run on macOS" >&2
    exit 1
fi

arch="$(uname -m)"
case "$arch" in
    arm64|x86_64) ;;
    *)
        echo "error: unsupported macOS architecture: $arch" >&2
        exit 1
        ;;
esac

if ! xcode-select -p >/dev/null 2>&1; then
    echo "error: Xcode Command Line Tools are required." >&2
    echo "Run: xcode-select --install" >&2
    exit 1
fi

missing=()
for tool in git cmake ninja python3; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        missing+=("$tool")
    fi
done

if (( ${#missing[@]} > 0 )); then
    echo "Missing required tools: ${missing[*]}" >&2
    if command -v brew >/dev/null 2>&1; then
        echo "Install with Homebrew, for example: brew install cmake ninja" >&2
    else
        echo "Homebrew is not required by Astraea, but it is a convenient way to install CMake and Ninja." >&2
    fi
    exit 1
fi

echo "Astraea macOS development environment"
echo "  OS:           $(sw_vers -productVersion)"
echo "  Architecture: $arch"
echo "  Git:          $(git --version)"
echo "  CMake:        $(cmake --version | head -n 1)"
echo "  Ninja:        $(ninja --version)"
echo "  Compiler:     $(c++ --version | head -n 1)"
echo "  Python:       $(python3 --version)"
echo
if [[ "$arch" == "arm64" ]]; then
    echo "Apple Silicon detected: portable Astraea components run locally."
    echo "Native x86-64 guest-execution tests will run on x86-64 hosts/CI."
fi
echo
echo "Environment check passed."
echo "Next:"
echo "  cmake --preset macos-dev"
echo "  cmake --build --preset macos-dev"
echo "  ctest --preset macos-dev"
