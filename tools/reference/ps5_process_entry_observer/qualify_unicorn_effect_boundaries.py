#!/usr/bin/env python3
"""Source-owned Unicorn 2.1.4 effect-boundary qualification (research only).

The earlier one-write-per-instruction trace is not a general x86 contract.
This independently authored corpus probes wide stores, repeated writes,
restartable partial faults and a single-thread RMW operation in mediated
virtual guest memory. Hook notifications are *observations*, not proof of
committed writes. No Sony code, native guest entry or runtime adoption.
"""
import sys

CODE = 0x100000
DATA = 0x300000
STACK = 0x400000
PAGE = 4096


def require(ok, reason):
    if not ok:
        raise AssertionError(reason)


def owned(code):
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_64
    from unicorn import UC_PROT_ALL, UC_PROT_READ, UC_PROT_EXEC
    from unicorn import UC_HOOK_MEM_WRITE
    from unicorn import x86_const as xr

    require(0 < len(code) <= 64, "bad owned instruction bounds")
    uc = Uc(UC_ARCH_X86, UC_MODE_64)
    uc.mem_map(CODE, PAGE, UC_PROT_ALL)
    uc.mem_write(CODE, code)
    uc.mem_protect(CODE, PAGE, UC_PROT_READ | UC_PROT_EXEC)
    uc.mem_map(DATA, PAGE, UC_PROT_ALL)
    uc.mem_map(STACK, PAGE, UC_PROT_ALL)
    uc.reg_write(xr.UC_X86_REG_RIP, CODE)
    uc.reg_write(xr.UC_X86_REG_EFLAGS, 2)
    uc.reg_write(xr.UC_X86_REG_RSP, STACK + 0x800)
    observed = []

    def on_write(_engine, _access, address, size, value, _data):
        require(0 < size <= 64, "unknown memory callback size")
        # The callback's integer value is at most a 64-bit C integer.
        # Do not mistake it for the complete payload of a 16-byte store.
        observed.append((address, size, value))

    uc.hook_add(UC_HOOK_MEM_WRITE, on_write)
    return uc, xr, observed


def execute(uc, code_bytes):
    # Bounded even if an implementation unexpectedly enters a loop.
    uc.emu_start(CODE, CODE + len(code_bytes), timeout=1_000_000, count=64)


def wide_vector_write():
    # MOVDQU [RBX],XMM0. The 128-bit payload cannot be inferred from
    # the 64-bit value supplied to a generic Unicorn memory hook.
    code = b"\xf3\x0f\x7f\x03"
    uc, xr, observed = owned(code)
    target = DATA + 0x80
    payload = bytes.fromhex("112233445566778899aabbccddeeff00")
    uc.reg_write(xr.UC_X86_REG_RBX, target)
    uc.reg_write(xr.UC_X86_REG_XMM0, int.from_bytes(payload, "little"))
    execute(uc, code)
    require(uc.reg_read(xr.UC_X86_REG_RIP) == CODE + len(code),
            "wide vector store did not retire")
    require(bytes(uc.mem_read(target, len(payload))) == payload,
            "128-bit store payload diverged")
    require(observed and all(target <= a and a + n <= target + 16
                             for a, n, _ in observed),
            "vector write hook range incorrect")
    return "wide_128_bit_write_readback"


def repeated_write():
    # REP MOVSB: multiple writes within one architectural instruction.
    code = b"\xf3\xa4"
    uc, xr, observed = owned(code)
    src = STACK + 0x100
    dst = DATA + 0x80
    payload = b"\x11\x22\x33\x44"
    uc.mem_write(src, payload)
    uc.reg_write(xr.UC_X86_REG_RSI, src)
    uc.reg_write(xr.UC_X86_REG_RDI, dst)
    uc.reg_write(xr.UC_X86_REG_RCX, len(payload))
    execute(uc, code)
    require(uc.reg_read(xr.UC_X86_REG_RIP) == CODE + 2 and
            uc.reg_read(xr.UC_X86_REG_RCX) == 0 and
            uc.reg_read(xr.UC_X86_REG_RSI) == src + len(payload) and
            uc.reg_read(xr.UC_X86_REG_RDI) == dst + len(payload),
            "REP MOVSB completed context differs")
    require(bytes(uc.mem_read(dst, 4)) == payload,
            "REP MOVSB guest bytes differ")
    require(observed and all(dst <= a and a + n <= dst + len(payload)
                             for a, n, _ in observed),
            "unexpected repeated-write callback target")
    return "rep_movsb_multiple_effects"


def interrupted_repeated_write():
    from unicorn import UcError, UC_PROT_READ, UC_PROT_ALL
    from unicorn import UC_ERR_WRITE_PROT, UC_HOOK_MEM_WRITE_PROT

    code = b"\xf3\xa4"
    uc, xr, observed = owned(code)
    src = STACK + 0x100
    first = DATA + PAGE - 1
    denied = DATA + PAGE
    uc.mem_map(denied, PAGE, UC_PROT_ALL)
    uc.mem_write(denied, b"\x77\x77\x77\x77")
    uc.mem_protect(denied, PAGE, UC_PROT_READ)
    uc.mem_write(src, b"\xa5\xb6\xc7")
    uc.reg_write(xr.UC_X86_REG_RSI, src)
    uc.reg_write(xr.UC_X86_REG_RDI, first)
    uc.reg_write(xr.UC_X86_REG_RCX, 3)
    faults = []

    def on_protected(_engine, _access, address, size, _value, _data):
        faults.append((address, size))
        return False

    uc.hook_add(UC_HOOK_MEM_WRITE_PROT, on_protected)
    try:
        execute(uc, code)
    except UcError as exc:
        require(exc.errno == UC_ERR_WRITE_PROT,
                "wrong fault code after partially committed REP")
    else:
        raise AssertionError("REP crossed into read-only destination without fault")

    require(bytes(uc.mem_read(first, 1)) == b"\xa5",
            "first iteration was not committed")
    require(bytes(uc.mem_read(denied, 4)) == b"\x77" * 4,
            "protected destination was modified")
    require(uc.reg_read(xr.UC_X86_REG_RIP) == CODE and
            uc.reg_read(xr.UC_X86_REG_RCX) == 2 and
            uc.reg_read(xr.UC_X86_REG_RSI) == src + 1 and
            uc.reg_read(xr.UC_X86_REG_RDI) == denied,
            "fault did not preserve restartable REP state")
    require(faults and faults[0] == (denied, 1),
            "protected write fault was not explicitly observed")
    # A hook notification alone cannot classify a committed effect.
    # We establish the one-byte prefix using readback and CPU state.
    require(any(a == first and n == 1 for a, n, _ in observed),
            "committed first iteration was not observable")
    return "rep_fault_keeps_committed_prefix_and_restart_state"


def locked_rmw():
    # LOCK XADD qword [RBX],RAX. Checks functional effects of a single
    # execution only; does NOT claim concurrency or atomicity validation.
    code = b"\xf0\x48\x0f\xc1\x03"
    uc, xr, observed = owned(code)
    target = DATA + 0x40
    uc.mem_write(target, (5).to_bytes(8, "little"))
    uc.reg_write(xr.UC_X86_REG_RBX, target)
    uc.reg_write(xr.UC_X86_REG_RAX, 7)
    execute(uc, code)
    require(uc.reg_read(xr.UC_X86_REG_RIP) == CODE + len(code) and
            uc.reg_read(xr.UC_X86_REG_RAX) == 5,
            "LOCK XADD register or RIP divergence")
    require(int.from_bytes(uc.mem_read(target, 8), "little") == 12,
            "LOCK XADD guest result divergence")
    require(observed and all(a == target and n == 8 for a, n, _ in observed),
            "LOCK XADD write hook divergence")
    return "locked_xadd_single_thread_only"


def main():
    from unicorn import __version__
    require(__version__ == "2.1.4", "unreviewed Unicorn version")
    for check in (wide_vector_write, repeated_write,
                  interrupted_repeated_write, locked_rmw):
        result = check()
        print("C1 UNICORN EFFECT CASE PASS: " + result)
    print("C1 UNICORN RESTARTABLE EFFECT BOUNDARY PASS")
    print("source_owned_effect_cases=4")
    print("faulting_rep=partial_guest_mutation_before_fault")
    print("faulting_rep_architecture=restartable_context")
    print("wide_write=readback_not_64_bit_hook_value")
    print("locked_rmw=single_thread_no_atomicity_claim")
    print("host_native_execution=none")
    print("sony_imports=unresolved")


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, ValueError, RuntimeError) as exc:
        print(f"C1 UNICORN EFFECT BOUNDARY REFUSED: {exc}", file=sys.stderr)
        sys.exit(1)
