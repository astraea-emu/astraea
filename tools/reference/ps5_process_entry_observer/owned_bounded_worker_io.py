#!/usr/bin/env python3
"""Bounded Linux child-process capture for source-owned C1 research only.

Read both pipes incrementally, enforcing caps before buffering rather than
checking a subprocess.run()/communicate() result after unbounded allocation.
Each child gets its own process group; on timeout, overflow or read failure,
kill that group and reap the direct child. Neither this helper nor captured
worker statements authenticate hostile execution or sandbox arbitrary code.

Linux-only research helper. Not a general hardened production supervisor.
"""
from dataclasses import dataclass
import os
import selectors
import signal
import subprocess
import time


@dataclass(frozen=True)
class BoundedResult:
    returncode: int
    stdout: bytes
    stderr: bytes


def _kill_child_group(proc):
    # start_new_session=True creates a child-owned process group; kill it
    # even when the leader exited and descendants still hold the pipes.
    try:
        os.killpg(proc.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    try:
        proc.kill()
    except ProcessLookupError:
        pass
    try:
        proc.wait(timeout=2)
    except subprocess.TimeoutExpired:
        raise ValueError("worker_unreaped_after_group_kill") from None


def capture_bounded(command, *, timeout_seconds, stdout_limit,
                    stderr_limit, env=None, preexec_fn=None):
    """Capture exact bounded bytes or reject; never retain unbounded output.

    preexec_fn is used only by the current single-threaded Linux controller
    for RLIMIT setup, not by a multithreaded/general-purpose caller.
    """
    if (not isinstance(command, (tuple, list)) or not command or
            any(not isinstance(s, str) or not s for s in command)):
        raise ValueError("worker_invalid_command")
    if (type(stdout_limit) is not int or type(stderr_limit) is not int or
            not 0 < stdout_limit <= (1 << 20) or
            not 0 < stderr_limit <= (1 << 20) or
            not isinstance(timeout_seconds, (int, float)) or
            not 0 < timeout_seconds <= 60):
        raise ValueError("worker_invalid_capture_budget")

    deadline = time.monotonic() + timeout_seconds
    proc = subprocess.Popen(
        command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, close_fds=True, start_new_session=True,
        env=env, preexec_fn=preexec_fn,
    )
    selector = selectors.DefaultSelector()
    caps = {"stdout": stdout_limit, "stderr": stderr_limit}
    contents = {"stdout": bytearray(), "stderr": bytearray()}
    try:
        for kind, stream in (("stdout", proc.stdout), ("stderr", proc.stderr)):
            os.set_blocking(stream.fileno(), False)
            selector.register(stream, selectors.EVENT_READ, kind)
        while selector.get_map():
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise ValueError("worker_io_deadline_exceeded")
            for key, _mask in selector.select(remaining):
                kind = key.data
                # Read at most one byte over the cap, never a larger chunk.
                size = min(4096, caps[kind] + 1 - len(contents[kind]))
                try:
                    block = os.read(key.fileobj.fileno(), size)
                except BlockingIOError:
                    continue
                if not block:
                    selector.unregister(key.fileobj)
                    continue
                contents[kind].extend(block)
                if len(contents[kind]) > caps[kind]:
                    raise ValueError("worker_" + kind + "_limit_exceeded")
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise ValueError("worker_io_deadline_exceeded")
        try:
            status = proc.wait(timeout=remaining)
        except subprocess.TimeoutExpired:
            raise ValueError("worker_io_deadline_exceeded") from None
        return BoundedResult(status, bytes(contents["stdout"]),
                             bytes(contents["stderr"]))
    except BaseException:
        _kill_child_group(proc)
        raise
    finally:
        selector.close()
        proc.stdout.close()
        proc.stderr.close()
