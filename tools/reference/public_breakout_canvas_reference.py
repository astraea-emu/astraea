#!/usr/bin/env python3
"""Source-pinned host CPU canvas reference for sharpemu-demo.

The independent author's game source is compiled unchanged in a temporary
directory. This is not guest emulation, PS5 VideoOut, or game compatibility.
"""
from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

PINNED_GAME_C_BLOB = "c231e5053352f4a0400b6d4c29b23220bba8f1a9"
PINNED_GAME_H_BLOB = "fe965b86648240a01db62d7d0e30cc5741fec78b"
MAX_SOURCE_BYTES = 128 * 1024
MAIN_SEED = "0xdecafbad"
ALTERNATE_SEED = "0x12345"
PIXEL_COUNT = 480 * 270
# First measured Linux Ubuntu 24.04 GCC 13.3 source-only result,
# CI 38095224065. This is a frozen CPU reference, not PS5 truth.
PINNED_MAIN_FINGERPRINT = "11521d6d6940fe57"
PINNED_ALTERNATE_FINGERPRINT = "8ed0d34c72e028e9"


class OracleRefusal(ValueError):
    pass


def exact_git_blob(path: Path, expected: str) -> bytes:
    with path.open("rb") as stream:
        data = stream.read(MAX_SOURCE_BYTES + 1)
    if len(data) > MAX_SOURCE_BYTES:
        raise OracleRefusal("source_size_limit")
    prefix = b"blob " + str(len(data)).encode("ascii") + b"\0"
    if hashlib.sha1(prefix + data).hexdigest() != expected:
        raise OracleRefusal(f"source_blob_identity_drift:{path.name}")
    return data


def canvas_report(output: str) -> tuple[int, str]:
    fields: dict[str, str] = {}
    for line in output.strip().splitlines():
        if line.count("=") != 1:
            raise OracleRefusal("malformed_oracle_output")
        key, value = line.split("=", 1)
        if key in fields:
            raise OracleRefusal("duplicate_oracle_field")
        fields[key] = value
    if set(fields) != {
        "canvas_width", "canvas_height", "nonzero_pixels", "canvas_fnv1a64"
    }:
        raise OracleRefusal("missing_oracle_field")
    if fields["canvas_width"] != "480" or fields["canvas_height"] != "270":
        raise OracleRefusal("unexpected_canvas_dimensions")
    nonzero = int(fields["nonzero_pixels"])
    digest = fields["canvas_fnv1a64"]
    if not (0 < nonzero <= PIXEL_COUNT):
        raise OracleRefusal("empty_or_invalid_canvas")
    if re.fullmatch(r"[0-9a-f]{16}", digest) is None:
        raise OracleRefusal("malformed_canvas_digest")
    return nonzero, digest


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--game-c", required=True, type=Path)
    parser.add_argument("--game-h", required=True, type=Path)
    args = parser.parse_args()
    try:
        source_c = exact_git_blob(args.game_c, PINNED_GAME_C_BLOB)
        source_h = exact_git_blob(args.game_h, PINNED_GAME_H_BLOB)
        with tempfile.TemporaryDirectory(prefix="astraea-breakout-canvas-") as tmp:
            root = Path(tmp)
            (root / "game.c").write_bytes(source_c)
            (root / "game.h").write_bytes(source_h)
            binary = root / "canvas-oracle"
            compiler = os.environ.get("CC", "cc")
            harness = Path(__file__).with_suffix(".c").resolve()
            build = subprocess.run(
                [compiler, "-std=c11", "-O0", "-fno-fast-math",
                 "-ffp-contract=off", "-fno-strict-aliasing",
                 str(root / "game.c"), str(harness), "-I", str(root),
                 "-lm", "-o", str(binary)],
                capture_output=True, text=True, timeout=120, check=False,
            )
            if build.returncode != 0:
                raise OracleRefusal("host_source_compile_failed:" + build.stderr[:1200])

            def run(seed: str) -> tuple[int, str]:
                result = subprocess.run(
                    [str(binary), seed], capture_output=True, text=True,
                    timeout=20, check=False,
                )
                if result.returncode != 0:
                    raise OracleRefusal("host_source_execution_failed")
                return canvas_report(result.stdout)

            first = run(MAIN_SEED)
            second = run(MAIN_SEED)
            alternate = run(ALTERNATE_SEED)
            if first != second:
                raise OracleRefusal("same_binary_repeat_diverged")
            if first[1] == alternate[1]:
                raise OracleRefusal("different_seeds_produced_identical_canvas")
            if first[1] != PINNED_MAIN_FINGERPRINT:
                raise OracleRefusal("pinned_primary_canvas_regression")
            if alternate[1] != PINNED_ALTERNATE_FINGERPRINT:
                raise OracleRefusal("pinned_alternate_canvas_regression")
        print("BREAKOUT CPU CANVAS REFERENCE PASS")
        print("independent_source_revision=49e6b3b25678f14ec57a70b907ee215fddb608d8")
        print("source_game_c_git_blob=" + PINNED_GAME_C_BLOB)
        print("source_game_h_git_blob=" + PINNED_GAME_H_BLOB)
        print("canvas_width=480")
        print("canvas_height=270")
        print(f"main_seed={MAIN_SEED}")
        print(f"nonzero_pixels={first[0]}")
        print(f"canvas_fnv1a64={first[1]}")
        print(f"alternate_seed_fnv1a64={alternate[1]}")
        print("same_binary_repeat=identical")
        print("pinned_cpu_reference=matched")
        print("guest_execution=not_attempted")
        print("ps5_video_out=not_attempted")
        print("emulator_frame=not_claimed")
        return 0
    except (OracleRefusal, OSError, ValueError, subprocess.TimeoutExpired) as exc:
        print("BREAKOUT CPU CANVAS REFERENCE REFUSED: " + str(exc),
              file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
