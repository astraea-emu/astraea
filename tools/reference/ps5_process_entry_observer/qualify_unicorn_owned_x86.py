#!/usr/bin/env python3
"""Conformance probes for a disposable, separately installed x86-64 engine.

All instruction encodings below are Astraea-authored. Never load a Sony
service, execute native host guest bytes, or allow an unbounded instruction
run. This probes Unicorn 2.1.4 as a CPU candidate, not as a PS5 runtime.
"""
import sys

CODE = 0x100000
DATA = 0x300000
STACK = 0x400000
PAGE = 4096
MAX_STEPS = 2
FULL64 = (1 << 64) - 1

def require(value, reason):
    if not value:
        raise AssertionError(reason)

def fixture(code):
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_64
    from unicorn import UC_PROT_ALL, UC_PROT_READ, UC_PROT_EXEC
    from unicorn import UC_HOOK_MEM_WRITE
    from unicorn import x86_const as xr

    require(0 < len(code) <= PAGE, "invalid owner-authored code bounds")
    uc = Uc(UC_ARCH_X86, UC_MODE_64)
    uc.mem_map(CODE, PAGE, UC_PROT_ALL)
    uc.mem_write(CODE, code)
    uc.mem_protect(CODE, PAGE, UC_PROT_READ | UC_PROT_EXEC)
    uc.mem_map(DATA, PAGE, UC_PROT_ALL)
    uc.mem_map(STACK, PAGE, UC_PROT_ALL)
    uc.reg_write(xr.UC_X86_REG_RIP, CODE)
    uc.reg_write(xr.UC_X86_REG_RSP, STACK + 0x800)
    uc.reg_write(xr.UC_X86_REG_EFLAGS, 2)
    seen = []

    def on_write(_uc, _kind, address, size, value, _user_data):
        require(0 < size <= 8, "unqualified Unicorn store width")
        blob = (value & ((1 << (size * 8)) - 1)).to_bytes(size, "little")
        seen.append((address, blob))

    uc.hook_add(UC_HOOK_MEM_WRITE, on_write)
    return uc, xr, seen

def step(uc, xr, expected_rip, count=1):
    require(0 < count <= MAX_STEPS, "owner-authored instruction budget")
    require(uc.reg_read(xr.UC_X86_REG_RIP) == expected_rip,
            "pre-step RIP mismatch")
    uc.emu_start(expected_rip, 0, timeout=0, count=count)

def expect_fault(uc, xr, expected_error, label):
    from unicorn import UcError
    before_rip = uc.reg_read(xr.UC_X86_REG_RIP)
    try:
        step(uc, xr, before_rip)
    except UcError as ex:
        require(ex.errno == expected_error,
                f"{label}: unexpected fault code {ex.errno}")
    else:
        raise AssertionError(f"{label}: fault was not raised")
    require(uc.reg_read(xr.UC_X86_REG_RIP) == before_rip,
            f"{label}: denied instruction incorrectly retired")

def run():
    from unicorn import (
        __version__, UC_PROT_READ, UC_ERR_WRITE_PROT,
        UC_ERR_WRITE_UNMAPPED, UC_ERR_FETCH_PROT,
        UC_ERR_INSN_INVALID
    )
    require(__version__ == "2.1.4",
            "unreviewed candidate version, refuse to qualify")
    passed = []

    def success(label):
        passed.append(label)
        print("C1 OWNED UNICORN CASE PASS: " + label)

    # MOV rax, imm64: verify complete architectural transfer, not only RIP.
    uc, xr, writes = fixture(b"\x48\xb8\x2a\x00\x00\x00\x00\x00\x00\x00")
    step(uc, xr, CODE)
    require(uc.reg_read(xr.UC_X86_REG_RAX) == 42 and
            uc.reg_read(xr.UC_X86_REG_RIP) == CODE + 10 and not writes,
            "mov rax immediate semantics")
    success("mov_rax_imm64")

    # PUSH r64: implicit 8-byte stack write and source register unchanged.
    uc, xr, writes = fixture(b"\x50")
    value = 0x1122334455667788
    sp = STACK + 0x800
    uc.reg_write(xr.UC_X86_REG_RAX, value)
    step(uc, xr, CODE)
    expected = value.to_bytes(8, "little")
    require(uc.reg_read(xr.UC_X86_REG_RSP) == sp - 8 and
            uc.reg_read(xr.UC_X86_REG_RAX) == value and
            uc.reg_read(xr.UC_X86_REG_RIP) == CODE + 1 and
            writes == [(sp - 8, expected)] and
            bytes(uc.mem_read(sp - 8, 8)) == expected,
            "implicit PUSH memory/register effects")
    success("push_implicit_stack_store")

    # CALL rel32 / RET: test real stack return word and two separate retires.
    code = b"\xe8\x0b\x00\x00\x00" + (b"\x90" * 11) + b"\xc3"
    uc, xr, writes = fixture(code)
    sp = STACK + 0x800
    step(uc, xr, CODE)
    ret_word = (CODE + 5).to_bytes(8, "little")
    require(uc.reg_read(xr.UC_X86_REG_RIP) == CODE + 16 and
            uc.reg_read(xr.UC_X86_REG_RSP) == sp - 8 and
            writes == [(sp - 8, ret_word)] and
            bytes(uc.mem_read(sp - 8, 8)) == ret_word,
            "CALL rel32 return address/stack")
    writes.clear()
    step(uc, xr, CODE + 16)
    require(uc.reg_read(xr.UC_X86_REG_RIP) == CODE + 5 and
            uc.reg_read(xr.UC_X86_REG_RSP) == sp and not writes,
            "RET stack read/control transfer")
    success("call_rel32_ret_stack_roundtrip")

    # SUB defines CF/PF/AF/ZF/SF/OF; zero and parity must be set.
    uc, xr, writes = fixture(b"\x48\x83\xe8\x01")
    uc.reg_write(xr.UC_X86_REG_RAX, 1)
    step(uc, xr, CODE)
    flags = uc.reg_read(xr.UC_X86_REG_EFLAGS)
    arithmetic_mask = 1 | 4 | 16 | 64 | 128 | 2048
    require(uc.reg_read(xr.UC_X86_REG_RAX) == 0 and
            flags & arithmetic_mask == 4 | 64 and not writes,
            "SUB arithmetic defined flags")
    success("sub_defined_flags")

    # AND RSP,-16: AF is *architecturally undefined* and is not compared.
    uc, xr, writes = fixture(b"\x48\x83\xe4\xf0")
    sp = STACK + 0x8ff
    uc.reg_write(xr.UC_X86_REG_RSP, sp)
    step(uc, xr, CODE)
    flags = uc.reg_read(xr.UC_X86_REG_EFLAGS)
    defined_mask = 1 | 4 | 64 | 128 | 2048
    require(uc.reg_read(xr.UC_X86_REG_RSP) == (sp & ~15) and
            (flags & (1 | 2048)) == 0 and
            (flags & defined_mask) == 4 and not writes,
            "AND defined bits/alignment; AF unclaimed")
    success("and_defined_flags_with_undefined_af")

    # Explicit 8-byte guest store and readback under RW permission.
    uc, xr, writes = fixture(b"\x48\x89\x03")
    value = 0xdeadbeef12345678
    addr = DATA + 0x80
    uc.reg_write(xr.UC_X86_REG_RAX, value)
    uc.reg_write(xr.UC_X86_REG_RBX, addr)
    step(uc, xr, CODE)
    payload = value.to_bytes(8, "little")
    require(writes == [(addr, payload)] and
            bytes(uc.mem_read(addr, 8)) == payload and
            uc.reg_read(xr.UC_X86_REG_RIP) == CODE + 3,
            "explicit guest store bytes/size/address")
    success("mov_qword_guest_store")

    # Reading RO memory is permitted, writing it is not.
    uc, xr, writes = fixture(b"\x48\x8b\x03")
    addr = DATA + 0x80
    value = 0x1122334455667788
    uc.mem_write(addr, value.to_bytes(8, "little"))
    uc.mem_protect(DATA, PAGE, UC_PROT_READ)
    uc.reg_write(xr.UC_X86_REG_RBX, addr)
    step(uc, xr, CODE)
    require(uc.reg_read(xr.UC_X86_REG_RAX) == value and not writes,
            "read-only guest memory must be readable")
    success("read_from_readonly_guest_mapping")

    # Exact same source-authored store targeting RO memory must fault.
    uc, xr, writes = fixture(b"\xc6\x00\x7f")
    uc.reg_write(xr.UC_X86_REG_RAX, DATA + 0x80)
    uc.mem_protect(DATA, PAGE, UC_PROT_READ)
    original = bytes(uc.mem_read(DATA + 0x80, 8))
    expect_fault(uc, xr, UC_ERR_WRITE_PROT, "readonly_write")
    require(bytes(uc.mem_read(DATA + 0x80, 8)) == original,
            "RO guest bytes changed following denied store")
    success("write_readonly_guest_denied")

    # Unmapped guest address must fault before storing any value.
    uc, xr, writes = fixture(b"\xc6\x00\x7f")
    uc.reg_write(xr.UC_X86_REG_RAX, 0x600000)
    expect_fault(uc, xr, UC_ERR_WRITE_UNMAPPED, "unmapped_write")
    require(bytes(uc.mem_read(DATA, 8)) == b"\x00" * 8,
            "unrelated mapped memory mutated")
    success("write_unmapped_guest_denied")

    # A non-executable guest code page must refuse even a harmless NOP.
    uc, xr, writes = fixture(b"\x90")
    uc.mem_protect(CODE, PAGE, UC_PROT_READ)
    expect_fault(uc, xr, UC_ERR_FETCH_PROT, "nonexecutable_fetch")
    success("execute_readonly_nonexec_denied")

    # UD2 must not be silently treated as a NOP/unsupported service success.
    uc, xr, writes = fixture(b"\x0f\x0b")
    expect_fault(uc, xr, UC_ERR_INSN_INVALID, "ud2")
    success("ud2_illegal_instruction_typed")

    # Instruction-count limit is enforced by emulator, even when later NOPs
    # are accessible. No unbounded guest loop or host syscall can follow.
    uc, xr, writes = fixture(b"\x90\x90\x90")
    step(uc, xr, CODE, count=2)
    require(uc.reg_read(xr.UC_X86_REG_RIP) == CODE + 2 and not writes,
            "finite instruction budget was not enforced")
    success("bounded_two_nop_retirements")

    require(len(passed) == 12 and len(set(passed)) == 12,
            "corpus accidentally lost a required case")
    print("C1 SOURCE-OWNED UNICORN X86-64 FAULT CORPUS PASS")
    print("source_owned_cases=12")
    print("code_fixtures=independently_authored")
    print("memory_permissions=readonly_unmapped_nonexecutable_checked")
    print("guest_stack=implicit_call_push_ret_verified")
    print("undefined_and_AF=not_assumed")
    print("engine=unicorn_2.1.4_external_research_only")
    print("native_breakout_admission=not_authorized")
    print("sony_services=not_executed")

if __name__ == "__main__":
    try:
        run()
    except (AssertionError, ValueError, RuntimeError) as exc:
        print(f"C1 OWNED UNICORN CORPUS REFUSED: {exc}", file=sys.stderr)
        sys.exit(1)
