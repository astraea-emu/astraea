#!/usr/bin/env python3
"""Linux x86-64 Landlock FS-mutation denial for reviewed C1 research only.

Requires Landlock ABI >= 3; refuses on unavailable/unsupported kernels.
A ruleset with NO allow-paths rejects all explicitly handled mutation rights,
while allowing path reads. Apply BEFORE seccomp denial probes to show that
Landlock itself, not the separate BPF policy, refuses attempted mutations.

Landlock does not revoke pre-opened descriptors, prevent all ioctls/metadata
changes, isolate arbitrary in-memory native execution, or authenticate
worker output. It is not a complete hostile-code sandbox.
"""
import ctypes
import errno
import os
import platform
import sys

LANDLOCK_CREATE_RULESET = 444       # x86-64 Linux syscall
LANDLOCK_RESTRICT_SELF = 446
LANDLOCK_CREATE_RULESET_VERSION = 1
PR_SET_NO_NEW_PRIVS = 38
MIN_ABI = 3

# linux/landlock.h, ABI <= 3: write, removal, creation, rename/refer and
# truncate. Do not handle read or execute so the research engine can load.
WRITE_FILE = 1 << 1
REMOVE_DIR = 1 << 4
REMOVE_FILE = 1 << 5
MAKE_CHAR = 1 << 6
MAKE_DIR = 1 << 7
MAKE_REG = 1 << 8
MAKE_SOCK = 1 << 9
MAKE_FIFO = 1 << 10
MAKE_BLOCK = 1 << 11
MAKE_SYM = 1 << 12
REFER = 1 << 13
TRUNCATE = 1 << 14
DENIED_FS_RIGHTS = (
    WRITE_FILE | REMOVE_DIR | REMOVE_FILE | MAKE_CHAR |
    MAKE_DIR | MAKE_REG | MAKE_SOCK | MAKE_FIFO | MAKE_BLOCK |
    MAKE_SYM | REFER | TRUNCATE
)
SENTINEL = b"ASTRAEA_C1_LANDLOCK_CANARY_V1"


def demand(condition, message):
    if not condition:
        raise ValueError(message)


def syschecked(libc, number, *args):
    ctypes.set_errno(0)
    value = libc.syscall(number, *args)
    if value < 0:
        raise ValueError(
            "landlock_syscall_%d_errno_%d" %
            (number, ctypes.get_errno())
        )
    return value


def install_fs_mutation_denial():
    """Install a non-optional, fail-closed FS restriction on current thread."""
    demand(sys.platform == "linux" and platform.machine() == "x86_64",
           "landlock_linux_x86_64_only")
    libc = ctypes.CDLL(None, use_errno=True)
    libc.syscall.restype = ctypes.c_long
    libc.prctl.restype = ctypes.c_int
    abi = syschecked(libc, LANDLOCK_CREATE_RULESET,
                     ctypes.c_void_p(), ctypes.c_size_t(0),
                     ctypes.c_uint32(LANDLOCK_CREATE_RULESET_VERSION))
    demand(abi >= MIN_ABI, "landlock_requires_abi_3")
    handled = ctypes.c_uint64(DENIED_FS_RIGHTS)
    fd = syschecked(libc, LANDLOCK_CREATE_RULESET,
                    ctypes.byref(handled), ctypes.c_size_t(8),
                    ctypes.c_uint32(0))
    try:
        demand(libc.prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0,
               "landlock_no_new_privs_failure")
        syschecked(libc, LANDLOCK_RESTRICT_SELF,
                   ctypes.c_int(fd), ctypes.c_uint32(0))
    finally:
        os.close(fd)
    return abi


def qualify_mutation_denials(canary_path):
    """Genuine kernel access checks, BEFORE installing the seccomp denylist.

    Only a caller-created scratch canary is addressed. A failed kernel
    restriction may allow an open/delete/create: fail the test immediately.
    The parent separately inspects the canary after the child exits.
    """
    demand(type(canary_path) is str and os.path.isabs(canary_path) and
           os.path.isfile(canary_path) and
           not os.path.islink(canary_path),
           "landlock_owned_canary_required")
    with open(canary_path, "rb") as source:
        demand(source.read(64) == SENTINEL, "landlock_canary_drift")
    abi = install_fs_mutation_denial()

    # Reads remain possible; these operations must be independently denied
    # by Landlock rather than by the later, separately installed seccomp.
    with open(canary_path, "rb") as source:
        demand(source.read(64) == SENTINEL, "landlock_read_drift")

    attempted = (
        ("open_write", lambda: os.open(canary_path, os.O_WRONLY)),
        ("unlink", lambda: os.unlink(canary_path)),
        ("mkdir", lambda: os.mkdir(canary_path + ".newdir")),
    )
    for label, operation in attempted:
        try:
            result = operation()
        except OSError as exc:
            demand(exc.errno == errno.EACCES,
                   "landlock_" + label + "_wrong_errno")
        else:
            if label == "open_write":
                os.close(result)
            raise ValueError("landlock_" + label + "_admitted")
    demand(os.path.isfile(canary_path), "landlock_canary_disappeared")
    return abi
