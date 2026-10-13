#!/usr/bin/env python3
"""External Unicorn 2.1.4 source-owned REP execution evidence producer.

This is NOT an attested generic post-commit memory hook, a PS5 process,
or a production backend. Both fixtures have fixed code, memory, register
expectations and a small bounded instruction budget. The resulting
transcript records values verified after actual mediated execution.
"""
import sys

CODE = 0x100000
DATA = 0x300000
STACK = 0x400000
PAGE = 4096
SOURCE = STACK + 0x100
CODE_BYTES = bytes.fromhex("f3a4")
REGISTERS = (
    "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "rsp",
    "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
    "rip", "rflags", "fs_base", "gs_base",
)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def snapshot(uc, xr):
    identifiers = {
        n: getattr(xr, "UC_X86_REG_" +
                   ("EFLAGS" if n == "rflags" else n.upper()))
        for n in REGISTERS
    }
    return {name: uc.reg_read(reg) for name, reg in identifiers.items()}


def run(case):
    from unicorn import (
        Uc, UcError, __version__, UC_ARCH_X86, UC_MODE_64,
        UC_PROT_ALL, UC_PROT_READ, UC_PROT_EXEC, UC_ERR_WRITE_PROT,
        UC_HOOK_MEM_WRITE, UC_HOOK_MEM_WRITE_PROT,
    )
    from unicorn import x86_const as xr
    require(__version__ == "2.1.4", "unreviewed Unicorn version")
    require(case in ("complete", "partial"), "unrecognized fixed fixture")

    uc = Uc(UC_ARCH_X86, UC_MODE_64)
    uc.mem_map(CODE, PAGE, UC_PROT_ALL)
    uc.mem_write(CODE, CODE_BYTES)
    uc.mem_protect(CODE, PAGE, UC_PROT_READ | UC_PROT_EXEC)
    uc.mem_map(DATA, PAGE, UC_PROT_ALL)
    uc.mem_map(STACK, PAGE, UC_PROT_ALL)

    complete = case == "complete"
    payload = bytes.fromhex("11223344" if complete else "a5b6c7")
    dest = DATA + (0x80 if complete else PAGE - 1)
    if not complete:
        uc.mem_map(DATA + PAGE, PAGE, UC_PROT_ALL)
        uc.mem_write(DATA + PAGE, bytes.fromhex("77777777"))
        uc.mem_protect(DATA + PAGE, PAGE, UC_PROT_READ)

    uc.mem_write(SOURCE, payload)
    initial = {name: 0 for name in REGISTERS}
    initial.update(rip=CODE, rsp=STACK + 0x800, rflags=2,
                   rcx=len(payload), rsi=SOURCE, rdi=dest)
    for name, value in initial.items():
        reg = ("EFLAGS" if name == "rflags" else name.upper())
        uc.reg_write(getattr(xr, "UC_X86_REG_" + reg), value)
    before = snapshot(uc, xr)
    require(before == initial, "initial full CPU state was not frozen")

    attempted = []
    denied = []

    def on_write(_engine, _kind, address, size, _value, _data):
        attempted.append((address, size))

    def on_denied(_engine, _kind, address, size, _value, _data):
        denied.append((address, size))
        return False

    uc.hook_add(UC_HOOK_MEM_WRITE, on_write)
    uc.hook_add(UC_HOOK_MEM_WRITE_PROT, on_denied)
    faulted = False
    try:
        uc.emu_start(CODE, CODE + len(CODE_BYTES),
                     timeout=1_000_000, count=64)
    except UcError as exc:
        require(not complete and exc.errno == UC_ERR_WRITE_PROT,
                "wrong execution exception/phase")
        faulted = True
    require(faulted == (not complete), "unexpected completion/fault")

    after = snapshot(uc, xr)
    ncommitted = len(payload) if complete else 1
    expected = dict(initial)
    expected["rip"] = CODE + len(CODE_BYTES) if complete else CODE
    expected["rcx"] -= ncommitted
    expected["rsi"] += ncommitted
    expected["rdi"] += ncommitted
    require(after == expected, "post-execution full CPU context divergence")

    committed_bytes = payload[:ncommitted]
    require(bytes(uc.mem_read(dest, ncommitted)) == committed_bytes,
            "completed bytes failed guest memory readback")
    require(bytes(uc.mem_read(SOURCE, len(payload))) == payload,
            "guest source bytes modified")
    for address in range(dest, dest + ncommitted):
        require((address, 1) in attempted,
                "committed write has no matching attempted access")
    if complete:
        require(not denied, "successful REP encountered a protection fault")
    else:
        require(denied == [(DATA + PAGE, 1)],
                "wrong exact read-only fault address/size")
        require(bytes(uc.mem_read(DATA + PAGE, 4)) == b"\x77" * 4,
                "denied destination was modified")
        require(bytes(uc.mem_read(DATA + PAGE - 1, 1)) == b"\xa5",
                "partial committed prefix was not preserved")

    # Fixed, bounded, strictly parsed text protocol consumed by an entirely
    # separate C++ research verifier. No user-supplied guest code.
    def cpu_line(name, registers):
        return name + "|" + ",".join(
            f"{registers[n]:016x}" for n in REGISTERS)

    lines = [
        f"C1EFFECT|1|{case}|{CODE_BYTES.hex()}",
        cpu_line("BEFORE", before),
        cpu_line("AFTER", after),
        ("OUTCOME|retired|none|-"
         if complete else
         f"OUTCOME|fault_after_effects|memory_permission|{DATA + PAGE:x}"),
    ]
    for i, value in enumerate(committed_bytes):
        lines.append(f"WRITE|{dest + i:x}|{value:02x}")
    lines.append("END")
    print("\n".join(lines))


def main():
    try:
        require(len(sys.argv) == 2, "expected fixed complete/partial case")
        run(sys.argv[1])
    except (AssertionError, ValueError, RuntimeError) as exc:
        print(f"C1 EXTERNAL UNICORN EFFECT PRODUCER REFUSED: {exc}",
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
