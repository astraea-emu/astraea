#!/usr/bin/env python3
"""Prepare a bounded source-owned native-title variant of the pinned public toolchain."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

PIN = "2f672d1c2f508e26f82ce6e27cef289a0861413c"
OLD = 'link_inputs=("$build/obj/app_crt.o" "$build/obj/app_cpp_runtime.o" "\${objects[@]}")'
NEW = 'link_inputs=("$build/obj/app_crt.o" "\${objects[@]}")'
ENTRY = """
/* Astraea-authored minimal entry; no Sony process ABI is inferred. */
extern "C" __attribute__((naked, visibility("default"))) void _start() {
    __asm__ volatile("ud2");
}
/* Converter's test-only GOT storage, not a runtime service. */
__asm__(".pushsection .got,\\\"aw\\\",@progbits\\n"
        ".quad 0\\n"
        ".popsection\\n");
"""
MARKER = "/* Owned sample; no runtime imports. */\n" + (
    "__attribute__((used)) unsigned long astraea_minimal_marker = "
    "0x4153545241454101UL;\n"
)

def git(path: Path, *args: str) -> str:
    result = subprocess.run(["git", "-C", str(path), *args],
        capture_output=True, text=True, timeout=15, check=True)
    return result.stdout.strip()

def prepare(path: Path) -> dict:
    if git(path, "rev-parse", "HEAD") != PIN:
        raise ValueError("unpinned third-party source")
    if git(path, "status", "--porcelain", "--untracked-files=all"):
        raise ValueError("expected clean detached worktree")
    build = path / "tools/build.sh"
    crt = path / "tooling/native/app_crt.cpp"
    original = build.read_text("utf-8")
    if original.count(OLD) != 1:
        raise ValueError("public linker input contract changed")
    original_crt = crt.read_text("utf-8")
    if "void\n_start(void *process_parameters" not in original_crt:
        raise ValueError("public CRT entry contract changed")
    source = path / "astraea_minimal"
    if source.exists():
        raise ValueError("minimal source already present")
    source.mkdir()
    (source / "marker.c").write_text(MARKER, "utf-8")
    crt.write_text(ENTRY, "utf-8")
    build.write_text(original.replace(OLD, NEW), "utf-8")
    return dict(source_commit=PIN, execution="none",
        policy="synthetic_entry_first_ud2",
        minimal_crt_sha256=hashlib.sha256(ENTRY.encode()).hexdigest(),
        minimal_marker_sha256=hashlib.sha256(MARKER.encode()).hexdigest())

def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--checkout", type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(prepare(args.checkout.resolve()), sort_keys=True))

if __name__ == "__main__":
    main()
