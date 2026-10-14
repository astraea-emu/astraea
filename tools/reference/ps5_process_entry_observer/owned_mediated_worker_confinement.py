#!/usr/bin/env python3
"""Linux x86-64 mediated-worker capability research; NOT a retail sandbox.

A source-owned x86 program runs in a disposable child via external Unicorn
2.1.4, never as host-native guest instructions. A kernel seccomp filter
denies selected host file/network/process operations after engine setup.
The parent checks a bounded report against fixed independent fixture
expectations and a full detached guest-memory publication transaction.

This is intentionally a NARROW negative capability probe, not a complete
default-deny host sandbox. It does not authenticate arbitrary worker
statements, confine memory corruption in the external engine, or authorize
outside-authored native code / commercial guest entry.
"""
import base64
import ctypes
import errno
import hashlib
import json
import os
import platform
import resource
import socket
import subprocess
import sys
import threading

from owned_guest_byte_access_policy import (
    GuestRegion, GuestRegionPolicy, PAGE_BYTES, READ, WRITE,
)
from owned_guest_memory_publication import (
    GuestMemoryPublication, ProposedGuestWrite,
)

CODE = 0x10000
DATA = 0x30000
STACK = 0x50000
TARGET = DATA + 0x10
# mov eax,11223344; mov [rdi],rax; add rax,1; mov [rdi+8],rax;
# mov rbx,[rdi]; xor rbx,rax; ud2
OWNED_CODE = bytes.fromhex(
    "b8443322114889074883c00148894708488b1f4831c30f0b"
)
CODE_SHA = "0738dfe2932275848edbd09cab90021df44b686907923aac7e4e12a5f0f0175c"
RIP_SEQUENCE = [CODE + n for n in (0, 5, 8, 12, 16, 19, 22)]
FIRST = 0x11223344
SECOND = FIRST + 1
MAX_REPORT_BYTES = 16384

# Linux x86-64 syscall numbers: deliberately partial resource policy.
# No claim is made about other ABIs, ioctl, mapped-file effects or all
# remaining allowed syscalls. All non-listed calls are still ALLOWED.
DENIED_SYSCALLS = (
    2, 41, 42, 43, 49, 50, 53, 57, 58, 59, 62, 85, 87,
    101, 165, 166, 257, 258, 263, 264, 288, 316, 322, 437,
)
ARCH_X86_64 = 0xC000003E
SECCOMP_RET_KILL_PROCESS = 0x80000000
SECCOMP_RET_ERRNO = 0x00050000
SECCOMP_RET_ALLOW = 0x7FFF0000


class SockFilter(ctypes.Structure):
    _fields_ = [("code", ctypes.c_ushort),
                ("jt", ctypes.c_ubyte),
                ("jf", ctypes.c_ubyte),
                ("k", ctypes.c_uint32)]


class SockFprog(ctypes.Structure):
    _fields_ = [("length", ctypes.c_ushort),
                ("filter", ctypes.POINTER(SockFilter))]


def require(condition, message):
    if not condition:
        raise ValueError(message)


def install_research_filter():
    """Deny selected host operations and fail shut on arch/x32 mismatch."""
    require(sys.platform == "linux" and platform.machine() == "x86_64",
            "linux_x86_64_worker_only")
    insns = [
        (0x20, 0, 0, 4),  # seccomp_data.arch
        (0x15, 1, 0, ARCH_X86_64),
        (0x06, 0, 0, SECCOMP_RET_KILL_PROCESS),
        (0x20, 0, 0, 0),  # seccomp_data.nr
        (0x45, 0, 1, 0x40000000),  # refuse x32 syscall-number bit
        (0x06, 0, 0, SECCOMP_RET_KILL_PROCESS),
    ]
    for number in DENIED_SYSCALLS:
        insns += [(0x15, 0, 1, number),
                  (0x06, 0, 0, SECCOMP_RET_ERRNO | errno.EPERM)]
    # glibc/QEMU may request clone3 for a pthread. Returning ENOSYS
    # triggers its legacy clone fallback. Never allow clone3 directly.
    insns += [
        (0x15, 0, 1, 435),
        (0x06, 0, 0, SECCOMP_RET_ERRNO | errno.ENOSYS),
        # Legacy clone: admit CLONE_THREAD only. A fork-like clone
        # without CLONE_THREAD is refused with EPERM. Offset 16 is
        # seccomp_data.args[0] (low 32 bits of unsigned flags).
        (0x15, 0, 4, 56),
        (0x20, 0, 0, 16),
        (0x45, 1, 0, 0x00010000),
        (0x06, 0, 0, SECCOMP_RET_ERRNO | errno.EPERM),
        (0x06, 0, 0, SECCOMP_RET_ALLOW),
    ]
    insns.append((0x06, 0, 0, SECCOMP_RET_ALLOW))
    array = (SockFilter * len(insns))(
        *(SockFilter(*item) for item in insns))
    prog = SockFprog(len(insns), array)
    libc = ctypes.CDLL(None, use_errno=True)
    libc.prctl.restype = ctypes.c_int
    # Set no_new_privs before the kernel accepts an unprivileged BPF filter.
    require(libc.prctl(38, 1, 0, 0, 0) == 0,
            "no_new_privs_install_failed")
    require(libc.prctl(22, 2, ctypes.byref(prog), 0, 0) == 0,
            "seccomp_filter_install_failed")


def denied_host_probes():
    """These calls must be denied by the kernel, not mocked in Python."""
    result = []
    probes = (
        ("open", lambda: os.open("/dev/null", os.O_RDONLY)),
        ("socket", lambda: socket.socket(socket.AF_INET, socket.SOCK_STREAM)),
        ("fork", os.fork),
    )
    for name, action in probes:
        try:
            handle = action()
        except OSError as exc:
            require(exc.errno == errno.EPERM, "unexpected_" + name + "_errno")
        else:
            # Keep an unexpectedly admitted fork from proceeding with tests.
            if name == "fork" and handle == 0:
                os._exit(86)
            raise ValueError("host_resource_operation_admitted:" + name)
        result.append(name + ":EPERM")
    return result


def worker():
    from unicorn import (
        Uc, UcError, UC_ARCH_X86, UC_MODE_64, UC_PROT_ALL,
        UC_PROT_READ, UC_PROT_EXEC, UC_HOOK_CODE,
        UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_ERR_INSN_INVALID,
        __version__ as version,
    )
    from unicorn import x86_const as x

    require(version == "2.1.4", "unpinned_mediated_engine")
    engine = Uc(UC_ARCH_X86, UC_MODE_64)
    engine.mem_map(CODE, PAGE_BYTES, UC_PROT_ALL)
    engine.mem_write(CODE, OWNED_CODE)
    engine.mem_protect(CODE, PAGE_BYTES, UC_PROT_READ | UC_PROT_EXEC)
    engine.mem_map(DATA, PAGE_BYTES, UC_PROT_ALL)
    engine.mem_map(STACK, PAGE_BYTES, UC_PROT_ALL)
    engine.reg_write(x.UC_X86_REG_RIP, CODE)
    engine.reg_write(x.UC_X86_REG_RDI, TARGET)
    engine.reg_write(x.UC_X86_REG_RSP, STACK + PAGE_BYTES // 2)
    engine.reg_write(x.UC_X86_REG_EFLAGS, 2)

    retired_or_attempted = []
    writes = []
    reads = []

    def code_hook(_engine, address, _size, _data):
        retired_or_attempted.append(address)

    def write_hook(_engine, _access, address, size, value, _data):
        writes.append([address, size, value])

    def read_hook(_engine, _access, address, size, _value, _data):
        reads.append([address, size])

    engine.hook_add(UC_HOOK_CODE, code_hook)
    engine.hook_add(UC_HOOK_MEM_WRITE, write_hook)
    engine.hook_add(UC_HOOK_MEM_READ, read_hook)

    # Install after importing and mapping the external engine, with no
    # inherited nonstandard FDs. This restricts host effects from this
    # point, but does not isolate the Python heap or all Linux syscalls.
    install_research_filter()
    denials = denied_host_probes()
    # QEMU/Unicorn legitimately requires host pthreads. Prove the
    # distinction: CLONE_THREAD succeeds but fork-like clone fails.
    completed = []
    thread = threading.Thread(target=lambda: completed.append(True))
    thread.start()
    thread.join(timeout=2)
    require(not thread.is_alive() and completed == [True],
            "host_pthread_runtime_refused")
    caught = None
    try:
        engine.emu_start(CODE, 0, timeout=1_000_000, count=16)
    except UcError as exc:
        caught = exc.errno
    require(caught == UC_ERR_INSN_INVALID, "invalid_instruction_not_observed")

    report = {
        "schema": 1,
        "source_sha256": CODE_SHA,
        "engine": version,
        "terminal": "typed_invalid_instruction",
        "rip_sequence": retired_or_attempted,
        "writes": writes,
        "reads": reads,
        "final_page": base64.b64encode(
            bytes(engine.mem_read(DATA, PAGE_BYTES))).decode("ascii"),
        "registers": {
            "rip": engine.reg_read(x.UC_X86_REG_RIP),
            "rax": engine.reg_read(x.UC_X86_REG_RAX),
            "rbx": engine.reg_read(x.UC_X86_REG_RBX),
            "rdi": engine.reg_read(x.UC_X86_REG_RDI),
            "rsp": engine.reg_read(x.UC_X86_REG_RSP),
            "rflags": engine.reg_read(x.UC_X86_REG_EFLAGS),
        },
        "host_denials": denials,
        "host_thread": "joined",
    }
    print(json.dumps(report, sort_keys=True, separators=(",", ":")))


def verify_report(report):
    require(type(report) is dict and set(report) == {
        "schema", "source_sha256", "engine", "terminal",
        "rip_sequence", "writes", "reads", "final_page",
        "registers", "host_denials", "host_thread",
    }, "worker_protocol_schema_refused")
    require(report["schema"] == 1 and
            report["source_sha256"] == CODE_SHA and
            report["engine"] == "2.1.4" and
            report["terminal"] == "typed_invalid_instruction",
            "worker_identity_or_terminal_refused")
    require(report["rip_sequence"] == RIP_SEQUENCE and
            report["reads"] == [[TARGET, 8]],
            "worker_instruction_or_read_trace_refused")
    require(report["writes"] == [[TARGET, 8, FIRST],
                                [TARGET + 8, 8, SECOND]],
            "worker_write_trace_refused")
    require(report["host_thread"] == "joined",
            "worker_host_thread_report_refused")
    require(report["host_denials"] == [
        "open:EPERM", "socket:EPERM", "fork:EPERM",
    ], "worker_host_denial_report_refused")
    regs = report["registers"]
    require(type(regs) is dict and set(regs) == {
        "rip", "rax", "rbx", "rdi", "rsp", "rflags",
    }, "worker_register_schema_refused")
    require(all(type(value) is int for value in regs.values()),
            "worker_register_width_refused")
    require(regs["rip"] == CODE + len(OWNED_CODE) - 2 and
            regs["rax"] == SECOND and regs["rbx"] == 1 and
            regs["rdi"] == TARGET and
            regs["rsp"] == STACK + PAGE_BYTES // 2,
            "worker_final_cpu_context_refused")
    # After XOR, CF/PF/ZF/SF/OF are defined and clear for result 1.
    # Do not assert the architecturally undefined AF bit.
    require(regs["rflags"] & (1 | 4 | 64 | 128 | 2048) == 0,
            "worker_defined_flag_bits_refused")
    require(type(report["final_page"]) is str and
            len(report["final_page"]) <= 2 * PAGE_BYTES,
            "worker_page_size_refused")
    try:
        final = base64.b64decode(report["final_page"], validate=True)
    except (ValueError, base64.binascii.Error) as exc:
        raise ValueError("worker_page_encoding_refused") from exc
    require(len(final) == PAGE_BYTES, "worker_page_length_refused")
    policy = GuestRegionPolicy([
        GuestRegion(DATA, DATA + PAGE_BYTES, READ | WRITE),
    ])
    tx = GuestMemoryPublication(policy, {DATA: bytes(PAGE_BYTES)})
    tx.verify({DATA: final}, [
        ProposedGuestWrite(TARGET, FIRST.to_bytes(8, "little")),
        ProposedGuestWrite(TARGET + 8, SECOND.to_bytes(8, "little")),
    ], "typed_invalid_instruction", terminal_attested=True)
    detached = tx.publish()
    require(detached.read(TARGET, 16) ==
            FIRST.to_bytes(8, "little") + SECOND.to_bytes(8, "little"),
            "worker_final_guest_memory_refused")
    return detached


def run_child():
    def limits():
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        resource.setrlimit(resource.RLIMIT_CPU, (3, 3))
        resource.setrlimit(resource.RLIMIT_AS, (2 << 30, 2 << 30))
        resource.setrlimit(resource.RLIMIT_FSIZE, (0, 0))
        resource.setrlimit(resource.RLIMIT_NOFILE, (32, 32))

    outcome = subprocess.run(
        [sys.executable, "-B", os.path.abspath(__file__), "--worker"],
        stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, timeout=12, check=False,
        close_fds=True, start_new_session=True, preexec_fn=limits,
        env={"PYTHONHASHSEED": "0", "PYTHONDONTWRITEBYTECODE": "1"},
    )
    require(outcome.returncode == 0 and
            0 < len(outcome.stdout) <= MAX_REPORT_BYTES,
            "mediated_worker_nonzero_or_unbounded_report:exit=" +
            str(outcome.returncode) + ":stderr=" +
            outcome.stderr.decode("utf-8", errors="replace")[-1000:])
    try:
        report = json.loads(outcome.stdout)
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ValueError("mediated_worker_invalid_json") from exc
    verify_report(report)
    return report


def mutation_tests(report):
    import copy
    changes = [
        ("terminal", lambda r: r.update(terminal="retired")),
        ("missing_host_denial", lambda r: r.update(host_denials=[])),
        ("invented_instruction", lambda r: r["rip_sequence"].append(CODE + 80)),
        ("forged_write", lambda r: r["writes"][0].__setitem__(2, FIRST + 1)),
        ("unexpected_read", lambda r: r["reads"].append([DATA, 1])),
        ("altered_cpu", lambda r: r["registers"].update(rbx=0)),
        ("extra_field", lambda r: r.update(undocumented=True)),
        ("altered_memory", lambda r: r.update(
            final_page=base64.b64encode(bytes(PAGE_BYTES)).decode("ascii"))),
    ]
    for label, change in changes:
        candidate = copy.deepcopy(report)
        change(candidate)
        try:
            verify_report(candidate)
        except ValueError:
            continue
        raise ValueError("tampered_worker_report_admitted:" + label)
    return len(changes)


def main():
    require(sys.platform == "linux" and platform.machine() == "x86_64",
            "linux_x86_64_only")
    require(hashlib.sha256(OWNED_CODE).hexdigest() == CODE_SHA,
            "frozen_source_instruction_identity_drift")
    if len(sys.argv) == 2 and sys.argv[1] == "--worker":
        worker()
        return
    require(len(sys.argv) == 1, "unknown_invocation")
    first = run_child()
    second = run_child()
    require(first == second, "two_fresh_worker_reports_diverged")
    negative_count = mutation_tests(first)
    print("C1 LINUX MEDIATED CHILD CAPABILITY GATE PASS")
    print("source_owned_x86_instructions_before_ud2=6")
    print("guest_data_writes=2_full_page_verified")
    print("guest_data_reads=1_independently_checked")
    print("fresh_worker_processes=2_identical")
    print("kernel_denied_host_operations=open_socket_fork")
    print("kernel_thread_exception=clone_thread_joined")
    print("controller_rejected_report_mutations=" + str(negative_count))
    print("external_cpu=unicorn_2.1.4_research_only")
    print("worker_seccomp_policy=partial_deny_not_full_sandbox")
    print("worker_report_authenticity=not_proven_for_hostile_workers")
    print("host_native_guest_entry=none")
    print("commercial_game_instruction_execution=none")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.TimeoutExpired) as exc:
        print("C1 MEDIATED WORKER REFUSED: " + str(exc), file=sys.stderr)
        sys.exit(1)
