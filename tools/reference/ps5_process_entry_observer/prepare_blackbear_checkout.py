#!/usr/bin/env python3
"""Prepare the pinned clean-room native-title checkout for Astraea C1 evidence.

This script performs no network or console operation. It refuses any checkout
other than the reviewed upstream revision and applies only exact, audited build
edits required to compile Astraea's pre-CRT observer.
"""

from __future__ import annotations

import argparse
import hashlib
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

PINNED_REVISION = "2f672d1c2f508e26f82ce6e27cef289a0861413c"
MARKER = ".astraea-ps5-entry-observer-v0"

CRT_BLOCK = r"""for name in app_crt app_cpp_runtime; do
    object="$build/obj/$name.o"
    ninja_inputs=("$native/$name.cpp" "$root/tooling/prospero-clang18" "$target_compiler")
    ninja_edge CXX "$object" env PS5_PAYLOAD_SDK="$sdk_root" \
        PS5_CLANG="$target_compiler" USE_CCACHE="${USE_CCACHE:-1}" \
        sh "$root/tooling/prospero-clang18" \
        -std=c++20 -O2 -Wall -Wextra -fno-exceptions -fno-rtti \
        -ffunction-sections -fdata-sections -MD -MF "$object.d" \
        -c "$native/$name.cpp" -o "$object"
done
"""

PATCHED_CRT_BLOCK = r"""for name in app_crt app_cpp_runtime; do
    object="$build/obj/$name.o"
    crt_definitions=()
    if [[ $name == app_crt ]]; then
        crt_definitions+=(-D_start=astraea_reference_crt_start)
    fi
    ninja_inputs=("$native/$name.cpp" "$root/tooling/prospero-clang18" "$target_compiler")
    ninja_edge CXX "$object" env PS5_PAYLOAD_SDK="$sdk_root" \
        PS5_CLANG="$target_compiler" USE_CCACHE="${USE_CCACHE:-1}" \
        sh "$root/tooling/prospero-clang18" \
        -std=c++20 -O2 -Wall -Wextra -fno-exceptions -fno-rtti \
        -ffunction-sections -fdata-sections "${crt_definitions[@]}" -MD -MF "$object.d" \
        -c "$native/$name.cpp" -o "$object"
done

observer_object="$build/obj/astraea_entry.o"
ninja_inputs=("$native/astraea_entry.S" "$root/tooling/prospero-clang18" "$target_compiler")
ninja_edge CC "$observer_object" env PS5_PAYLOAD_SDK="$sdk_root" \
    PS5_CLANG="$target_compiler" USE_CCACHE="${USE_CCACHE:-1}" \
    sh "$root/tooling/prospero-clang18" \
    -x assembler-with-cpp -DASTRAEA_PS5_ENTRY_SYMBOL=_start \
    -c "$native/astraea_entry.S" -o "$observer_object"
"""

LINK_INPUTS = 'link_inputs=("$build/obj/app_crt.o" "$build/obj/app_cpp_runtime.o" "${objects[@]}")'
PATCHED_LINK_INPUTS = 'link_inputs=("$build/obj/astraea_entry.o" "$build/obj/app_crt.o" "$build/obj/app_cpp_runtime.o" "${objects[@]}")'

AUTO_EMIT = """extern "C" void astraea_emit_ps5_entry_observation_v0() noexcept;

/*
 * _init_env runs before the CRT enters _init(). Priority 101 is the earliest
 * user-reserved constructor priority, so capture emission precedes ordinary
 * default-priority application constructors without becoming pre-entry work.
 */
__attribute__((constructor(101)))
static void astraea_emit_process_entry_observation() noexcept
{
    astraea_emit_ps5_entry_observation_v0();
}
"""


class PreparationError(RuntimeError):
    pass


def astraea_root() -> Path:
    return Path(__file__).resolve().parents[3]


def run_git(checkout: Path, *args: str) -> str:
    try:
        completed = subprocess.run(
            ["git", "-C", str(checkout), *args],
            check=True,
            capture_output=True,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError) as error:
        raise PreparationError(f"git {' '.join(args)} failed") from error
    return completed.stdout.strip()


def patch_build_script(text: str) -> str:
    if text.count(CRT_BLOCK) != 1:
        raise PreparationError(
            "pinned app_crt build block is missing or ambiguous"
        )
    if text.count(LINK_INPUTS) != 1:
        raise PreparationError(
            "pinned link-input declaration is missing or ambiguous"
        )
    text = text.replace(CRT_BLOCK, PATCHED_CRT_BLOCK)
    text = text.replace(LINK_INPUTS, PATCHED_LINK_INPUTS)
    return text


def verified_astraea_revision() -> str:
    root = astraea_root()
    tracked = (
        "tools/reference/ps5_process_entry_observer/entry.S",
        "tools/reference/ps5_process_entry_observer/capture.h",
        "tools/reference/ps5_process_entry_observer/emit_observation.cpp",
        "tools/reference/ps5_process_entry_observer/prepare_blackbear_checkout.py",
    )
    status = run_git(
        root,
        "status",
        "--porcelain",
        "--untracked-files=all",
        "--",
        *tracked,
    )
    if status:
        raise PreparationError(
            "Astraea observer/preparation sources are modified or untracked"
        )
    return run_git(root, "rev-parse", "HEAD")


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def copy_observer_sources(checkout: Path) -> None:
    source = astraea_root() / "tools/reference/ps5_process_entry_observer"
    native = checkout / "tooling/native"
    app = checkout / "src/astraea_observer"

    required = {
        source / "entry.S": native / "astraea_entry.S",
        source / "capture.h": app / "capture.h",
        source / "emit_observation.cpp": app / "emit_observation.cpp",
    }
    for src in required:
        if not src.is_file():
            raise PreparationError(f"missing Astraea observer source: {src}")

    if app.exists():
        raise PreparationError(
            "external checkout already contains src/astraea_observer"
        )
    if (native / "astraea_entry.S").exists():
        raise PreparationError(
            "external checkout already contains tooling/native/astraea_entry.S"
        )

    app.mkdir(parents=True)
    for src, dst in required.items():
        shutil.copyfile(src, dst)
    (app / "auto_emit.cpp").write_text(AUTO_EMIT, encoding="utf-8")


def prepare(checkout: Path) -> None:
    checkout = checkout.resolve()
    if not (checkout / ".git").exists():
        raise PreparationError("checkout is not a Git working tree")

    astraea_revision = verified_astraea_revision()

    revision = run_git(checkout, "rev-parse", "HEAD")
    if revision != PINNED_REVISION:
        raise PreparationError(
            f"expected upstream revision {PINNED_REVISION}, found {revision}"
        )

    status = run_git(checkout, "status", "--porcelain", "--untracked-files=all")
    if status:
        raise PreparationError("external checkout is not clean")

    marker = checkout / MARKER
    if marker.exists():
        raise PreparationError("observer preparation marker already exists")

    build_script = checkout / "tools/build.sh"
    if not build_script.is_file():
        raise PreparationError("pinned tools/build.sh is missing")

    original = build_script.read_text(encoding="utf-8")
    patched = patch_build_script(original)

    # Validate every mutation before writing any external file.
    copy_targets = (
        checkout / "tooling/native/astraea_entry.S",
        checkout / "src/astraea_observer",
    )
    if any(path.exists() for path in copy_targets):
        raise PreparationError("observer destination already exists")

    build_script.write_text(patched, encoding="utf-8")
    try:
        copy_observer_sources(checkout)
        source_root = astraea_root() / "tools/reference/ps5_process_entry_observer"
        marker.write_text(
            "astraea.ps5.process-entry/v0\n"
            f"upstream={PINNED_REVISION}\n"
            f"astraea={astraea_revision}\n"
            f"entry_sha256={file_sha256(source_root / 'entry.S')}\n"
            f"capture_sha256={file_sha256(source_root / 'capture.h')}\n"
            f"emitter_sha256={file_sha256(source_root / 'emit_observation.cpp')}\n",
            encoding="utf-8",
        )
    except Exception:
        build_script.write_text(original, encoding="utf-8")
        shutil.rmtree(checkout / "src/astraea_observer", ignore_errors=True)
        try:
            (checkout / "tooling/native/astraea_entry.S").unlink()
        except FileNotFoundError:
            pass
        raise


def self_test() -> None:
    patched = patch_build_script(
        "# prefix\n" + CRT_BLOCK + "\n" + LINK_INPUTS + "\n# suffix\n"
    )
    assert PATCHED_CRT_BLOCK in patched
    assert PATCHED_LINK_INPUTS in patched
    assert CRT_BLOCK not in patched
    assert patched.count("astraea_reference_crt_start") == 1
    assert "assembler-with-cpp" in patched
    assert "\\\n        PS5_CLANG=" in CRT_BLOCK
    assert "\\\n        PS5_CLANG=" in PATCHED_CRT_BLOCK

    try:
        patch_build_script(patched)
    except PreparationError:
        pass
    else:
        raise AssertionError("already-patched build text was not rejected")

    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        (root / "tooling/native").mkdir(parents=True)
        (root / "src/astraea_observer").mkdir(parents=True)
        (root / "src/astraea_observer/auto_emit.cpp").write_text(
            AUTO_EMIT,
            encoding="utf-8",
        )
        auto_emit = (
            root / "src/astraea_observer/auto_emit.cpp"
        ).read_text(encoding="utf-8")
        assert "constructor(101)" in auto_emit

        shell = root / "patched-build-fragment.sh"
        shell.write_text(
            "#!/usr/bin/env bash\n"
            "build=build\nroot=.\nnative=tooling/native\n"
            "target_compiler=clang\nsdk_root=sdk\n"
            "objects=()\n"
            + PATCHED_CRT_BLOCK
            + "\n"
            + PATCHED_LINK_INPUTS
            + "\n",
            encoding="utf-8",
        )
        subprocess.run(
            ["bash", "-n", str(shell)],
            check=True,
            capture_output=True,
            text=True,
        )

    # Exercise the complete mutation transaction against a synthetic real Git
    # checkout. Temporarily pin to the synthetic commit so revision and
    # cleanliness gates are tested without network access.
    with tempfile.TemporaryDirectory() as directory:
        checkout = Path(directory) / "external"
        (checkout / "tools").mkdir(parents=True)
        (checkout / "tooling/native").mkdir(parents=True)
        (checkout / "src").mkdir(parents=True)
        (checkout / "tools/build.sh").write_text(
            "#!/usr/bin/env bash\n"
            + CRT_BLOCK
            + "\n"
            + LINK_INPUTS
            + "\n",
            encoding="utf-8",
        )

        subprocess.run(["git", "init", str(checkout)], check=True, capture_output=True)
        subprocess.run(
            ["git", "-C", str(checkout), "config", "user.email", "astraea@example.invalid"],
            check=True,
        )
        subprocess.run(
            ["git", "-C", str(checkout), "config", "user.name", "Astraea Self Test"],
            check=True,
        )
        subprocess.run(
            ["git", "-C", str(checkout), "add", "."],
            check=True,
        )
        subprocess.run(
            ["git", "-C", str(checkout), "commit", "-m", "fixture"],
            check=True,
            capture_output=True,
        )
        fixture_revision = run_git(checkout, "rev-parse", "HEAD")

        global PINNED_REVISION
        original_revision = PINNED_REVISION
        try:
            PINNED_REVISION = fixture_revision
            prepare(checkout)
            prepared = (checkout / "tools/build.sh").read_text(encoding="utf-8")
            assert PATCHED_CRT_BLOCK in prepared
            assert PATCHED_LINK_INPUTS in prepared
            assert (checkout / MARKER).is_file()
            marker_text = (checkout / MARKER).read_text(encoding="utf-8")
            assert "upstream=" + fixture_revision in marker_text
            assert "astraea=" in marker_text
            assert "entry_sha256=" in marker_text
            assert "capture_sha256=" in marker_text
            assert "emitter_sha256=" in marker_text
            assert (checkout / "tooling/native/astraea_entry.S").is_file()
            assert (checkout / "src/astraea_observer/capture.h").is_file()
            assert (checkout / "src/astraea_observer/emit_observation.cpp").is_file()
            assert (checkout / "src/astraea_observer/auto_emit.cpp").is_file()

            try:
                prepare(checkout)
            except PreparationError:
                pass
            else:
                raise AssertionError("second preparation of dirty checkout was not rejected")
        finally:
            PINNED_REVISION = original_revision


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--checkout",
        type=Path,
        help="clean local checkout of the pinned external native-title project",
    )
    parser.add_argument("--self-test", action="store_true")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if args.self_test:
        self_test()
        return 0
    if args.checkout is None:
        raise PreparationError("--checkout is required")
    prepare(args.checkout)
    print(
        "Prepared pinned external checkout for Astraea process-entry evidence.\n"
        "Next: run the external project's normal build, preserve build/llvm-pie.elf "
        "and build/eboot.elf, deploy only through your already-authorized workflow, "
        "and collect two ASTRAEA_ENTRY_V0 records."
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except PreparationError as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(2)
