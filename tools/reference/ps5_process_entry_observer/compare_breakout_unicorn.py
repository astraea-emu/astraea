#!/usr/bin/env python3
"""Separate-process, research-only Unicorn x86-64 vs Astraea Breakout trace.

No external engine is linked, vendored, or admitted to production Astraea.
No imported CALL retires. Fail closed on ANY structural/trace difference.
"""
import argparse
import hashlib
import re
import struct
import sys
import traceback
from pathlib import Path

PINNED_SHA256 = "05c414993d1cd9e7182bbf5647be1ffad72c6ed6b6f5399d01af27281fef48b8"
PAGE = 4096
MAX_ELF = 16 * 1024 * 1024
GPR = ("rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "rsp",
       "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15")
FIELDS = GPR + ("rflags", "fs_base", "gs_base")
AF = 1 << 4
FULL_FLAGS = (1 << 64) - 1


def check(condition, message):
    if not condition:
        raise ValueError(message)


def parse_reference(path):
    lines = Path(path).read_text(encoding="utf-8").splitlines()
    single = {}
    steps = {}
    mnemonics = {}
    for line in lines:
        if line.startswith("c1trace.step["):
            match = re.fullmatch(r"c1trace\.step\[(\d+)\]=(.+)", line)
            check(match is not None, "malformed trace event")
            index = int(match[1])
            check(index not in steps, "duplicate trace event")
            segments = match[2].split("|")
            check(len(segments) == 6, "malformed trace field count")
            before, after, addr, size, written, registers = segments
            regs = tuple(int(v, 16) for v in registers.split(","))
            check(len(regs) == len(FIELDS), "invalid full register context")
            size = int(size)
            check(size in (0, 8), "unsupported write width")
            check((addr == "-") == (size == 0), "missing/forged write address")
            check((written == "-") == (size == 0), "missing/forged write payload")
            blob = b"" if size == 0 else bytes.fromhex(written)
            check(len(blob) == size, "write payload size mismatch")
            steps[index] = (int(before, 16), int(after, 16),
                            None if addr == "-" else int(addr, 16),
                            blob, regs)
        elif line.startswith("c1trace."):
            key, sep, value = line.partition("=")
            check(sep == "=" and key not in single, "duplicate/malformed header")
            single[key] = value
        else:
            matched = re.fullmatch(
                r"source_x86_callee\[(\d+)\]\.mnemonic=(\w+)", line)
            if matched:
                check(int(matched[1]) not in mnemonics, "duplicate mnemonic")
                mnemonics[int(matched[1])] = matched[2].upper()
    required = ("version", "source_sha256", "entry", "stack_base",
                "stack_size", "initial_rsp", "initial_rflags",
                "relative_target", "relative_value", "unresolved_got",
                "unresolved_word", "stop_rip", "steps", "import_calls_retired")
    check(set(single) == {"c1trace." + x for x in required},
          "missing/unexpected trace header")
    check(single["c1trace.version"] == "1" and
          single["c1trace.source_sha256"] == PINNED_SHA256 and
          single["c1trace.import_calls_retired"] == "0",
          "unsupported trace version/source/import claim")
    count = int(single["c1trace.steps"])
    check(count == 13 and sorted(steps) == list(range(count)),
          "incomplete or reordered 13-step reference")
    check(len(mnemonics) >= 11, "missing source-backed callee mnemonics")
    for i in range(1, count):
        check(steps[i - 1][1] == steps[i][0],
              "discontinuous source-backed instruction trace")
    ref = {x: int(single["c1trace." + x], 16)
           for x in required if x not in
           ("version", "source_sha256", "steps", "import_calls_retired")}
    check(ref["entry"] == steps[0][0] and
          ref["stop_rip"] == steps[-1][1] == 0x23c and
          ref["unresolved_got"] == 0x68c8 and
          ref["relative_target"] == 0x6900 and
          ref["stack_size"] == PAGE and
          ref["initial_rflags"] == 2, "frozen entry/relocation/stop drift")
    return ref, steps, mnemonics


def map_original_loads(uc, elf):
    check(0 < len(elf) <= MAX_ELF and elf[:4] == b"\x7fELF" and
          elf[4] == 2 and elf[5] == 1, "not a little-endian ELF64")
    phoff = struct.unpack_from("<Q", elf, 32)[0]
    phentsize, phnum = struct.unpack_from("<HH", elf, 54)
    check(phentsize == 56 and 0 < phnum <= 32 and
          phoff + phentsize * phnum <= len(elf), "malformed program table")
    allocated = set()
    loads = 0
    for i in range(phnum):
        typ, flags, offset, va, _, filesz, memsz, _ = struct.unpack_from(
            "<IIQQQQQQ", elf, phoff + i * phentsize)
        if typ != 1:
            continue
        check(flags <= 7 and 0 < memsz <= MAX_ELF and filesz <= memsz
              and offset + filesz <= len(elf)
              and va + memsz <= (1 << 64), "invalid PT_LOAD")
        for page in range(va // PAGE * PAGE,
                          (va + memsz + PAGE - 1) // PAGE * PAGE, PAGE):
            check(page < 1 << 64 and len(allocated) < 4096,
                  "guest map exceeds research budget")
            if page not in allocated:
                # Backed only by Unicorn's mediated CPU memory, never a
                # host-native executable PS5 mapping. Original virtual W+X
                # semantics are retained rather than forced into host W^X.
                uc.mem_map(page, PAGE, 7)
                allocated.add(page)
        if filesz:
            uc.mem_write(va, elf[offset:offset + filesz])
        loads += 1
    check(loads >= 2, "missing pinned load segments")
    return allocated


def run(elf_path, reference_path):
    from unicorn import Uc, UcError, UC_ARCH_X86, UC_MODE_64, UC_HOOK_MEM_WRITE
    from unicorn import __version__ as unicorn_version
    from unicorn import x86_const as xr

    check(unicorn_version == "2.1.4",
          "unreviewed Unicorn version")
    elf = Path(elf_path).read_bytes()
    check(hashlib.sha256(elf).hexdigest() == PINNED_SHA256,
          "independent executable SHA drift")
    ref, steps, mnemonics = parse_reference(reference_path)
    uc = Uc(UC_ARCH_X86, UC_MODE_64)
    mapped = map_original_loads(uc, elf)
    stack_base = ref["stack_base"]
    check(stack_base % PAGE == 0 and
          stack_base not in mapped and
          stack_base <= ref["initial_rsp"] < stack_base + PAGE,
          "invalid or aliased synthetic stack")
    uc.mem_map(stack_base, PAGE, 7)
    uc.mem_write(ref["relative_target"],
                 ref["relative_value"].to_bytes(8, "little"))
    original_got = int.from_bytes(
        bytes(uc.mem_read(ref["unresolved_got"], 8)), "little")
    check(original_got == ref["unresolved_word"],
          "original unresolved GOT does not match source")

    register_ids = {name: getattr(xr, "UC_X86_REG_" + name.upper())
                    for name in GPR}
    register_ids.update({
        "rflags": xr.UC_X86_REG_EFLAGS,
        "fs_base": xr.UC_X86_REG_FS_BASE,
        "gs_base": xr.UC_X86_REG_GS_BASE,
    })
    for name in GPR:
        uc.reg_write(register_ids[name], ref["initial_rsp"] if name == "rsp" else 0)
    uc.reg_write(xr.UC_X86_REG_EFLAGS, ref["initial_rflags"])
    uc.reg_write(xr.UC_X86_REG_FS_BASE, 0)
    uc.reg_write(xr.UC_X86_REG_GS_BASE, 0)
    uc.reg_write(xr.UC_X86_REG_RIP, ref["entry"])
    writes = []

    def on_write(_engine, _kind, address, size, value, _user):
        check(size in (1, 2, 4, 8), "unqualified memory effect size")
        writes.append((address, int(value & ((1 << (size * 8)) - 1))
                       .to_bytes(size, "little")))

    uc.hook_add(UC_HOOK_MEM_WRITE, on_write)
    af_defined = True
    for i in range(13):
        before, after, address, blob, registers = steps[i]
        check(uc.reg_read(xr.UC_X86_REG_RIP) == before,
              f"pre-step RIP mismatch at {i}")
        writes.clear()
        try:
            # Count exactly ONE instruction; never issue a fourteenth resume
            # or transfer through the opaque first imported GOT slot.
            uc.emu_start(before, 0, timeout=0, count=1)
        except UcError as exc:
            raise ValueError(f"mediated engine fault before step {i} retires: {exc}")
        check(uc.reg_read(xr.UC_X86_REG_RIP) == after,
              f"post-step RIP mismatch at {i}")
        expected_effects = [] if address is None else [(address, blob)]
        check(writes == expected_effects,
              f"guest memory effects diverged at step {i}")
        if address is not None:
            check(bytes(uc.mem_read(address, len(blob))) == blob,
                  f"guest write readback mismatch at step {i}")
        for n, expected in zip(FIELDS, registers):
            actual = uc.reg_read(register_ids[n])
            if n == "rflags":
                # AF is undefined following AND until the first SUB
                # redefines it; undefined bits are not oracle claims.
                mnemonic = "AND" if i == 0 else mnemonics.get(i - 2, "")
                if mnemonic == "AND":
                    af_defined = False
                if mnemonic == "SUB":
                    af_defined = True
                mask = FULL_FLAGS if af_defined else FULL_FLAGS & ~AF
                check((actual & mask) == (expected & mask),
                      f"defined RFLAGS mismatch at step {i}")
            else:
                check(actual == expected, f"register {n} mismatch at step {i}")
    check(uc.reg_read(xr.UC_X86_REG_RIP) == ref["stop_rip"] == 0x23c,
          "original unsupported imported CALL boundary changed")
    check(int.from_bytes(bytes(uc.mem_read(ref["unresolved_got"], 8)),
                         "little") == ref["unresolved_word"],
          "unresolved import slot was modified")
    print("C1 UNICORN EXTERNAL MEDIATED BREAKOUT PARITY PASS")
    print("source_sha256=" + PINNED_SHA256)
    print("engine=unicorn_2.1.4_separate_research_process")
    print("original_breakout_commits=13")
    print("cpu_and_guest_write_parity=13_source_backed_steps")
    print("unresolved_import_call=unretired_at_0x23c")
    print("unbound_got=unchanged")
    print("sony_services=not_executed")
    print("runtime_license_adoption=not_authorized")
    print("production_guest_admission=unchanged")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original_elf")
    parser.add_argument("astraea_trace")
    args = parser.parse_args()
    try:
        run(args.original_elf, args.astraea_trace)
    except (ValueError, OSError, IndexError, KeyError, struct.error) as exc:
        print(f"C1 UNICORN PARITY REFUSED: {type(exc).__name__}: {exc}",
              file=sys.stderr)
        # On a frozen, hash-pinned research input, point to the exact
        # comparator operation that failed without catching/forgiving it.
        traceback.print_exc(limit=8, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
