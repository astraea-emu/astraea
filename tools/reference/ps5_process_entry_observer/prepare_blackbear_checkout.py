#!/usr/bin/env python3
"""Prepare the pinned BlackBear native-title checkout for Astraea C1 evidence.

The CLI is intentionally fail-closed:
- it performs no network access;
- it accepts only the pinned upstream commit;
- the external worktree must be clean;
- every source edit must match an exact expected snippet;
- --check previews the complete mutation without writing files.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

PINNED_UPSTREAM = "2f672d1c2f508e26f82ce6e27cef289a0861413c"
OBSERVER_FILES = ("entry.S", "capture.h", "emit_observation.cpp")
LOCAL_DIR = ".astraea-observer"


class PrepareError(RuntimeError):
    pass


def _git(repo: Path, *args: str) -> str:
    try:
        result = subprocess.run(
            ["git", "-C", str(repo), *args],
            check=True,
            capture_output=True,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError) as error:
        raise PrepareError(f"git {' '.join(args)} failed") from error
    return result.stdout.strip()


def _replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise PrepareError(
            f"{label}: expected one exact source match, found {count}"
        )
    return text.replace(old, new, 1)


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _transform_build(source: str) -> str:
    source = _replace_once(
        source,
        'for name in app_crt app_cpp_runtime; do\n'
        '    object="$build/obj/$name.o"\n',
        'for name in app_crt app_cpp_runtime; do\n'
        '    object="$build/obj/$name.o"\n'
        '    extra=()\n'
        '    [[ $name != app_crt ]] || '
        'extra+=(-D_start=astraea_reference_crt_start)\n',
        "tools/build.sh CRT loop",
    )
    source = _replace_once(
        source,
        '        sh "$root/tooling/prospero-clang18" \\\n'
        '        -std=c++20 -O2 -Wall -Wextra -fno-exceptions -fno-rtti \\\n',
        '        sh "$root/tooling/prospero-clang18" \\\n'
        '        "${extra[@]}" \\\n'
        '        -std=c++20 -O2 -Wall -Wextra -fno-exceptions -fno-rtti \\\n',
        "tools/build.sh CRT compile command",
    )

    old_link = (
        'done\n\n'
        'link_inputs=("$build/obj/app_crt.o" '
        '"$build/obj/app_cpp_runtime.o" "${objects[@]}")'
    )
    observer_block = r'''done

observer_dir="$root/.astraea-observer"
observer_entry="$build/obj/astraea_ps5_entry_observer.o"
ninja_inputs=(
    "$observer_dir/entry.S"
    "$root/tooling/prospero-clang18"
    "$target_compiler"
)
ninja_edge CXX "$observer_entry" env PS5_PAYLOAD_SDK="$sdk_root" \
    PS5_CLANG="$target_compiler" USE_CCACHE="${USE_CCACHE:-1}" \
    sh "$root/tooling/prospero-clang18" \
    -x assembler-with-cpp -Wno-unused-command-line-argument \
    -DASTRAEA_PS5_ENTRY_SYMBOL=_start \
    -MD -MF "$observer_entry.d" \
    -c "$observer_dir/entry.S" -o "$observer_entry"

observer_emitter="$build/obj/astraea_ps5_entry_emitter.o"
ninja_inputs=(
    "$observer_dir/emit_observation.cpp"
    "$observer_dir/capture.h"
    "$root/tooling/prospero-clang18"
    "$target_compiler"
)
ninja_edge CXX "$observer_emitter" env PS5_PAYLOAD_SDK="$sdk_root" \
    PS5_CLANG="$target_compiler" USE_CCACHE="${USE_CCACHE:-1}" \
    sh "$root/tooling/prospero-clang18" \
    -std=c++20 -O2 -Wall -Wextra -fno-exceptions -fno-rtti \
    -ffunction-sections -fdata-sections -I"$observer_dir" \
    -MD -MF "$observer_emitter.d" \
    -c "$observer_dir/emit_observation.cpp" -o "$observer_emitter"

link_inputs=(
    "$observer_entry"
    "$build/obj/app_crt.o"
    "$build/obj/app_cpp_runtime.o"
    "$observer_emitter"
    "${objects[@]}"
)'''
    source = _replace_once(
        source,
        old_link,
        observer_block,
        "tools/build.sh observer link insertion",
    )
    return source


def _transform_main(source: str) -> str:
    source = _replace_once(
        source,
        '#include <span>\n\nnamespace\n',
        '#include <span>\n\n'
        'extern "C" void astraea_emit_ps5_entry_observation_v0() noexcept;\n\n'
        'namespace\n',
        "src/main.cpp emitter declaration",
    )
    source = _replace_once(
        source,
        'int main()\n{\n',
        'int main()\n{\n'
        '    astraea_emit_ps5_entry_observation_v0();\n',
        "src/main.cpp emitter call",
    )
    return source


def _observer_root() -> Path:
    return Path(__file__).resolve().parent


def _astraea_root() -> Path:
    return _observer_root().parents[2]


def prepare(
    checkout: Path,
    *,
    expected_commit: str = PINNED_UPSTREAM,
    observer_source: Path | None = None,
    observer_commit: str | None = None,
    dry_run: bool = False,
) -> dict[str, object]:
    checkout = checkout.resolve()
    if not (checkout / ".git").exists():
        raise PrepareError("checkout is not a Git worktree")

    head = _git(checkout, "rev-parse", "HEAD")
    if head != expected_commit:
        raise PrepareError(
            f"checkout HEAD {head} does not match pinned {expected_commit}"
        )

    status = _git(checkout, "status", "--porcelain", "--untracked-files=all")
    if status:
        raise PrepareError("checkout worktree is not clean")

    observer_source = (observer_source or _observer_root()).resolve()
    if observer_commit is None:
        try:
            observer_commit = _git(_astraea_root(), "rev-parse", "HEAD")
        except PrepareError:
            observer_commit = "unknown"

    source_bytes: dict[str, bytes] = {}
    for name in OBSERVER_FILES:
        path = observer_source / name
        if not path.is_file():
            raise PrepareError(f"observer source is missing {path}")
        source_bytes[name] = path.read_bytes()

    build_path = checkout / "tools" / "build.sh"
    main_path = checkout / "src" / "main.cpp"
    if not build_path.is_file() or not main_path.is_file():
        raise PrepareError("pinned checkout layout is incomplete")

    build_new = _transform_build(build_path.read_text(encoding="utf-8"))
    main_new = _transform_main(main_path.read_text(encoding="utf-8"))

    writes: dict[Path, bytes] = {
        Path("tools/build.sh"): build_new.encode(),
        Path("src/main.cpp"): main_new.encode(),
    }
    for name, data in source_bytes.items():
        writes[Path(LOCAL_DIR) / name] = data

    manifest = {
        "schema": "astraea.ps5.entry-observer-integration/v0",
        "upstream_commit": head,
        "astraea_observer_commit": observer_commit,
        "files": {
            str(path): _sha256(data)
            for path, data in sorted(writes.items(), key=lambda item: str(item[0]))
        },
    }
    manifest_bytes = (
        json.dumps(manifest, sort_keys=True, indent=2) + "\n"
    ).encode()
    writes[Path(LOCAL_DIR) / "manifest.json"] = manifest_bytes

    report = {
        "upstream_commit": head,
        "astraea_observer_commit": observer_commit,
        "dry_run": dry_run,
        "would_write": [str(path) for path in sorted(writes, key=str)],
    }

    if dry_run:
        return report

    for relative, data in writes.items():
        target = checkout / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)

    return report


BUILD_FIXTURE = r'''for name in app_crt app_cpp_runtime; do
    object="$build/obj/$name.o"
    ninja_inputs=("$native/$name.cpp" "$root/tooling/prospero-clang18" "$target_compiler")
    ninja_edge CXX "$object" env PS5_PAYLOAD_SDK="$sdk_root" \
        PS5_CLANG="$target_compiler" USE_CCACHE="${USE_CCACHE:-1}" \
        sh "$root/tooling/prospero-clang18" \
        -std=c++20 -O2 -Wall -Wextra -fno-exceptions -fno-rtti \
        -ffunction-sections -fdata-sections -MD -MF "$object.d" \
        -c "$native/$name.cpp" -o "$object"
done

link_inputs=("$build/obj/app_crt.o" "$build/obj/app_cpp_runtime.o" "${objects[@]}")
'''

MAIN_FIXTURE = '''#include "demo_renderer.hpp"

#include <array>
#include <span>

namespace
{
}

int main()
{
    return 0;
}
'''


class PrepareTests(unittest.TestCase):
    def make_checkout(self, root: Path) -> tuple[Path, str]:
        checkout = root / "checkout"
        (checkout / "tools").mkdir(parents=True)
        (checkout / "src").mkdir(parents=True)
        (checkout / "tools" / "build.sh").write_text(
            BUILD_FIXTURE, encoding="utf-8"
        )
        (checkout / "src" / "main.cpp").write_text(
            MAIN_FIXTURE, encoding="utf-8"
        )
        subprocess.run(["git", "init", str(checkout)], check=True, capture_output=True)
        subprocess.run(
            ["git", "-C", str(checkout), "config", "user.email", "test@example.invalid"],
            check=True,
        )
        subprocess.run(
            ["git", "-C", str(checkout), "config", "user.name", "Astraea Test"],
            check=True,
        )
        subprocess.run(
            ["git", "-C", str(checkout), "add", "."], check=True
        )
        subprocess.run(
            ["git", "-C", str(checkout), "commit", "-m", "fixture"],
            check=True,
            capture_output=True,
        )
        head = _git(checkout, "rev-parse", "HEAD")
        return checkout, head

    def make_observer(self, root: Path) -> Path:
        source = root / "observer"
        source.mkdir()
        for name in OBSERVER_FILES:
            (source / name).write_text(f"fixture:{name}\n", encoding="utf-8")
        return source

    def test_dry_run_does_not_mutate(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            checkout, head = self.make_checkout(root)
            observer = self.make_observer(root)
            before = _git(checkout, "status", "--porcelain", "--untracked-files=all")
            report = prepare(
                checkout,
                expected_commit=head,
                observer_source=observer,
                observer_commit="observer-test",
                dry_run=True,
            )
            self.assertTrue(report["dry_run"])
            self.assertFalse((checkout / LOCAL_DIR).exists())
            self.assertEqual(
                _git(checkout, "status", "--porcelain", "--untracked-files=all"),
                before,
            )

    def test_prepare_is_deterministic_and_records_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            checkout, head = self.make_checkout(root)
            observer = self.make_observer(root)
            prepare(
                checkout,
                expected_commit=head,
                observer_source=observer,
                observer_commit="observer-test",
            )
            build = (checkout / "tools" / "build.sh").read_text()
            main = (checkout / "src" / "main.cpp").read_text()
            self.assertIn("-D_start=astraea_reference_crt_start", build)
            self.assertIn("-DASTRAEA_PS5_ENTRY_SYMBOL=_start", build)
            self.assertIn('"$observer_entry"', build)
            self.assertIn("astraea_emit_ps5_entry_observation_v0();", main)
            manifest = json.loads(
                (checkout / LOCAL_DIR / "manifest.json").read_text()
            )
            self.assertEqual(manifest["upstream_commit"], head)
            self.assertEqual(
                manifest["astraea_observer_commit"], "observer-test"
            )
            for name in OBSERVER_FILES:
                self.assertEqual(
                    (checkout / LOCAL_DIR / name).read_text(),
                    f"fixture:{name}\n",
                )

    def test_dirty_tree_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            checkout, head = self.make_checkout(root)
            observer = self.make_observer(root)
            (checkout / "untracked.txt").write_text("dirty")
            with self.assertRaisesRegex(PrepareError, "not clean"):
                prepare(
                    checkout,
                    expected_commit=head,
                    observer_source=observer,
                    observer_commit="observer-test",
                )

    def test_wrong_revision_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            checkout, _ = self.make_checkout(root)
            observer = self.make_observer(root)
            with self.assertRaisesRegex(PrepareError, "does not match pinned"):
                prepare(
                    checkout,
                    expected_commit="0" * 40,
                    observer_source=observer,
                    observer_commit="observer-test",
                )

    def test_unknown_source_text_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            checkout, _ = self.make_checkout(root)
            (checkout / "tools" / "build.sh").write_text(
                "different upstream layout\n", encoding="utf-8"
            )
            subprocess.run(
                ["git", "-C", str(checkout), "add", "."], check=True
            )
            subprocess.run(
                ["git", "-C", str(checkout), "commit", "-m", "changed"],
                check=True,
                capture_output=True,
            )
            head = _git(checkout, "rev-parse", "HEAD")
            observer = self.make_observer(root)
            with self.assertRaisesRegex(PrepareError, "expected one exact"):
                prepare(
                    checkout,
                    expected_commit=head,
                    observer_source=observer,
                    observer_commit="observer-test",
                )


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("checkout", nargs="?", type=Path)
    parser.add_argument(
        "--check",
        action="store_true",
        help="validate and preview all writes without mutating the checkout",
    )
    parser.add_argument("--self-test", action="store_true")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    if args.self_test:
        suite = unittest.defaultTestLoader.loadTestsFromTestCase(PrepareTests)
        result = unittest.TextTestRunner(verbosity=2).run(suite)
        return 0 if result.wasSuccessful() else 1
    if args.checkout is None:
        raise PrepareError("checkout path is required")
    report = prepare(args.checkout, dry_run=args.check)
    print(json.dumps(report, sort_keys=True, indent=2))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except PrepareError as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(2)
