#!/usr/bin/env python3
"""Exercise the #334 offline CLI on genuine host-built ELFs with SYNTHETIC logs.

This never makes hardware observations. The fabricated records exist only in a
temporary directory; they test parser/analysis plumbing and are not evidence
about any real PS5 firmware, title, or loader.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path

from procparam_identity import (
    find_defined_symbol,
    find_procparam,
)


def cli_json(*arguments: str) -> dict[str, object]:
    command = [sys.executable, str(Path(__file__).with_name("procparam_identity.py"))]
    completed = subprocess.run(
        [*command, *arguments],
        check=True,
        text=True,
        capture_output=True,
    )
    return json.loads(completed.stdout)


def observation_line(
    *,
    capture_link: int,
    procparam_link: int,
    static_prefix: bytes,
    load_bias: int,
) -> str:
    # Fabricated pointers preserve ASLR-independent relationships deliberately.
    # They are NOT a claim about a PS5 RDI, RSI, RSP or procparam ABI.
    return (
        "ASTRAEA_ENTRY_V0 status=complete "
        f"capture_runtime=0x{load_bias + capture_link:016x} "
        f"rdi=0x{load_bias + 0x1234:016x} "
        f"rsi=0x{load_bias + 0x2000:016x} "
        "rbp=0x0000000000000000 "
        f"rsp=0x{load_bias + 0x4448:016x} "
        "process_prefix=01000000000000001122000000000000 "
        f"procparam_runtime=0x{load_bias + procparam_link:016x} "
        f"procparam_prefix={static_prefix.hex()}"
    )


def check(intermediate_path: Path, final_path: Path) -> None:
    intermediate = intermediate_path.read_bytes()
    final = final_path.read_bytes()
    capture_link = find_defined_symbol(
        intermediate, "astraea_ps5_entry_capture_v0", "intermediate ELF"
    )
    procparam, static_prefix = find_procparam(final, "final ELF")
    common = (
        "--intermediate", str(intermediate_path),
        "--final", str(final_path),
    )

    with tempfile.TemporaryDirectory(prefix="astraea-c1-synthetic-") as temp:
        root = Path(temp)
        paths = []
        for index, bias in enumerate((0x10000000, 0x20000000), start=1):
            path = root / f"synthetic-run{index}.log"
            path.write_text(
                "SYNTHETIC OFFLINE TEST; NOT REAL PS5 EVIDENCE\n"
                + observation_line(
                    capture_link=capture_link,
                    procparam_link=procparam.vaddr,
                    static_prefix=static_prefix,
                    load_bias=bias,
                )
                + "\n",
                encoding="utf-8",
            )
            paths.append(path)
            result = cli_json(*common, "--log-file", str(path))
            assert result["pointer_match"] is True, result
            assert result["prefix_match"] is True, result
            assert result["startup_parameters_distinct_from_api_return"] is True
            assert result["entry_projection"]["rsp_mod16"] == 8
            assert result["entry_projection"]["fs_base_nonzero"] is None
            assert result["entry_projection"]["gs_base_nonzero"] is None

        equivalent = cli_json(
            *common,
            "--compare-log-files", str(paths[0]), str(paths[1]),
        )
        assert equivalent["equivalent"] is True, equivalent
        assert equivalent["first_difference"] is None

        # An observed prefix disagreement must remain visible rather than
        # being normalized into false equivalence.
        divergent = root / "synthetic-divergent.log"
        divergent.write_text(
            paths[1].read_text(encoding="utf-8").replace(
                f"procparam_prefix={static_prefix.hex()}",
                f"procparam_prefix={'00' * 16}",
            ),
            encoding="utf-8",
        )
        mismatch = cli_json(
            *common, "--compare-log-files", str(paths[0]), str(divergent)
        )
        assert mismatch["equivalent"] is False, mismatch
        assert mismatch["first_difference"]["field"] == "prefix_match", mismatch

    print("SYNTHETIC OFFLINE PIPELINE PASS: real built ELF pair; CLI single runs,")
    print("ASLR-independent repeat equivalence and prefix-falsification checked.")
    print("NO CONSOLE OBSERVATION; NO #334 ABI EVIDENCE.")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--intermediate", type=Path, required=True)
    parser.add_argument("--final", dest="final_elf", type=Path, required=True)
    args = parser.parse_args()
    check(args.intermediate, args.final_elf)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
