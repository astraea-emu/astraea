#!/usr/bin/env python3
"""Adversarial source-owned controller I/O regression. No guest/game code."""
import json
from pathlib import Path
import sys
import tempfile
import time

from owned_bounded_worker_io import capture_bounded


def require(value, message):
    if not value:
        raise AssertionError(message)


def child(program, *, out=16384, err=4096, seconds=3):
    return capture_bounded(
        [sys.executable, "-B", "-c", program],
        timeout_seconds=seconds, stdout_limit=out, stderr_limit=err,
        env={"PYTHONDONTWRITEBYTECODE": "1"},
    )


def refused(label, program, expected, *, out=16384, err=4096,
            seconds=3):
    try:
        child(program, out=out, err=err, seconds=seconds)
    except ValueError as exc:
        require(str(exc) == expected,
                label + ":wrong_refusal:" + str(exc))
        return
    raise AssertionError(label + ":unexpected_admission")


def main():
    count = 0
    # Independent normal child returns exact byte streams, with no shell.
    good = child("import os; os.write(1,b'valid'); os.write(2,b'warn')")
    require((good.returncode, good.stdout, good.stderr) ==
            (0, b"valid", b"warn"), "positive_stream_identity")
    count += 1

    # Concurrent pipes larger than usual OS pipe capacity: both drained.
    both = child("import os; os.write(1,b'a'*70000); "
                 "os.write(2,b'b'*70000)", out=80000, err=80000)
    require((len(both.stdout), len(both.stderr)) == (70000, 70000),
            "simultaneous_large_stream_deadlock")
    count += 1

    exact = child("import os; os.write(1,b'x'*128)", out=128)
    require(exact.stdout == b"x" * 128, "exact_cap_rejected")
    count += 1

    refused("stdout_overflow",
            "import os; os.write(1,b'x'*131072)",
            "worker_stdout_limit_exceeded", out=1024)
    count += 1

    refused("stderr_overflow",
            "import os; os.write(2,b'x'*131072)",
            "worker_stderr_limit_exceeded", err=1024)
    count += 1

    refused("silent_hang", "import time; time.sleep(10)",
            "worker_io_deadline_exceeded", seconds=0.15)
    count += 1

    failed = child("import os; os.write(2,b'controlled'); os._exit(7)")
    require((failed.returncode, failed.stderr) == (7, b"controlled"),
            "nonzero_worker_exit_concealed")
    count += 1

    try:
        child("pass", out=0)
    except ValueError as exc:
        require(str(exc) == "worker_invalid_capture_budget",
                "invalid_budget_wrong_refusal")
    else:
        raise AssertionError("invalid_budget_admitted")
    count += 1

    raw = child("import os; os.write(1,b'{invalid-json')")
    try:
        json.loads(raw.stdout)
    except json.JSONDecodeError:
        pass
    else:
        raise AssertionError("bad_json_accepted_as_report")
    count += 1

    # The process leader exits without closing stdout/stderr in a child it
    # forked. A naive wait() on the leader could call that success and leave
    # the child alive. The selector must hit the deadline and kill the PGID.
    with tempfile.TemporaryDirectory() as temp:
        marker = Path(temp) / "descendant-wrote"
        code = (
            "import os,time; p=os.fork(); "
            "time.sleep(0.65) if p==0 else None; "
            f"open({str(marker)!r},'w').write('escaped') if p==0 else None; "
            "os._exit(0)"
        )
        refused("detached_pipe_descendant", code,
                "worker_io_deadline_exceeded", seconds=0.12)
        time.sleep(0.72)
        require(not marker.exists(), "stray_descendant_host_write")
    count += 1

    print("C1 BOUNDED MEDIATED WORKER IO NEGATIVES PASS")
    print(f"source_owned_capture_cases={count}")
    print("stdout_stderr_streaming_caps=enforced_before_allocation")
    print("concurrent_pipe_progress=checked")
    print("timeout_and_process_group_kill=checked")
    print("descendant_host_effect=absent_after_timeout")
    print("nonzero_exit_and_invalid_json=refused_by_controller_contract")
    print("retail_code_executed=none")
    print("hostile_worker_sandbox=not_established")


if __name__ == "__main__":
    main()
