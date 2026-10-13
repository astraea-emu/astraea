#!/usr/bin/env python3
"""Measure bounded mediated x86-64 execution modes, not PS5 performance.

The exact same Astraea-authored 256-instruction SUB workload is run through
a disposable Unicorn 2.1.4 instance in single-step, instruction-hook and
batch modes. Every run checks full guest CPU state and unchanged source.
Numbers are runner-specific RESEARCH telemetry, not host-native guest
execution, game FPS, or proof of a viable production CPU backend.
"""
import hashlib
import math
import os
import platform
import statistics
import sys
import time

CODE = 0x100000
PAGE = 4096
STEPS = 256
ENCODING = b"\x48\x83\xe8\x01"  # sub rax, 1
PROGRAM = ENCODING * STEPS
PROGRAM_SHA256 = "16460540c7d856baa1e96fd5d335b82c2bf872f5d84398a76b97ab1c8d2e193d"
WARMUPS = 4
REPEATS = 25
MODES = ("batch", "code_hook", "single_step")
GPR = ("rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "rsp",
       "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15")
REGISTERS = GPR + ("rip", "rflags", "fs_base", "gs_base")
ARITHMETIC_FLAGS = (1 << 0) | (1 << 2) | (1 << 4) | (1 << 6) | (1 << 7) | (1 << 11)


def check(condition, detail):
    if not condition:
        raise AssertionError(detail)


def check_budget(count):
    check(isinstance(count, int) and 0 < count <= STEPS,
          "invalid finite source-owned instruction budget")


def ids(xr):
    return {name: getattr(xr, "UC_X86_REG_" +
            ("EFLAGS" if name == "rflags" else name.upper()))
            for name in REGISTERS}


def setup(program=PROGRAM, observed=None):
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_64
    from unicorn import UC_PROT_ALL, UC_PROT_READ, UC_PROT_EXEC, UC_HOOK_CODE
    from unicorn import x86_const as xr
    check(len(program) == STEPS * len(ENCODING) <= PAGE,
          "source-owned program must fit one executable guest page")
    uc = Uc(UC_ARCH_X86, UC_MODE_64)
    uc.mem_map(CODE, PAGE, UC_PROT_ALL)
    uc.mem_write(CODE, program)
    uc.mem_protect(CODE, PAGE, UC_PROT_READ | UC_PROT_EXEC)
    if observed is not None:
        def on_code(_engine, address, size, _user):
            check(address >= CODE and (address - CODE) % len(ENCODING) == 0
                  and address < CODE + len(program) and
                  size == len(ENCODING), "unexpected source-owned code hook")
            observed.append(address)
        uc.hook_add(UC_HOOK_CODE, on_code, CODE, CODE + len(program) - 1)
    return uc, ids(xr)


def reset(uc, registers):
    for name, register in registers.items():
        initial = (STEPS if name == "rax" else CODE if name == "rip"
                   else 2 if name == "rflags" else 0)
        uc.reg_write(register, initial)


def state(uc, registers):
    return {name: uc.reg_read(reg) for name, reg in registers.items()}


def valid_final(uc, registers, program=PROGRAM, count=STEPS):
    cpu = state(uc, registers)
    target = CODE + len(ENCODING) * count
    if cpu["rip"] != target or cpu["rax"] != STEPS - count:
        return False
    if count == STEPS:
        if (cpu["rflags"] & ARITHMETIC_FLAGS) != ((1 << 2) | (1 << 6)):
            return False
    for name in REGISTERS:
        if name not in ("rip", "rax", "rflags") and cpu[name] != 0:
            return False
    return bytes(uc.mem_read(CODE, len(program))) == program


def execute(uc, registers, mode, observed=None, count=STEPS):
    check_budget(count)
    reset(uc, registers)
    if observed is not None:
        observed.clear()
    started = time.perf_counter_ns()
    if mode == "single_step":
        for i in range(count):
            rip = CODE + len(ENCODING) * i
            check(uc.reg_read(registers["rip"]) == rip,
                  "noncontinuous per-instruction guest RIP")
            uc.emu_start(rip, 0, timeout=0, count=1)
    elif mode in ("batch", "code_hook"):
        uc.emu_start(CODE, CODE + len(ENCODING) * STEPS,
                     timeout=0, count=count)
    else:
        raise AssertionError("unreviewed execution mode")
    elapsed = time.perf_counter_ns() - started
    check(elapsed > 0, "nonpositive execution measurement")
    check(valid_final(uc, registers, count=count),
          "engine CPU state, budget or code/source parity failed")
    if observed is not None:
        check(observed == [CODE + len(ENCODING) * i for i in range(count)],
              "instruction hook observations diverged")
    return elapsed


def negatives():
    check_budget(STEPS)
    for bad in (0, STEPS + 1, -1):
        try:
            check_budget(bad)
        except AssertionError:
            pass
        else:
            raise AssertionError("unbounded instruction budget admitted")

    uc, regs = setup()
    reset(uc, regs)
    uc.emu_start(CODE, CODE + len(PROGRAM), timeout=0, count=STEPS - 1)
    check(not valid_final(uc, regs),
          "truncated instruction run accepted as complete")

    altered = bytearray(PROGRAM)
    altered[-1] ^= 0x03  # Final SUB imm8: 0x01 -> 0x02
    check(altered[-1] == 0x02, "source mutation fixture drift")
    uc, regs = setup(bytes(altered))
    reset(uc, regs)
    uc.emu_start(CODE, CODE + len(PROGRAM), timeout=0, count=STEPS)
    check(not valid_final(uc, regs, bytes(altered)),
          "altered opcode passed original CPU expectation")
    print("C1 MEDIATED CPU SOURCE/BUDGET NEGATIVES PASS")


def measurement(mode):
    observed = [] if mode == "code_hook" else None
    uc, registers = setup(observed=observed)
    cold_ns = execute(uc, registers, mode, observed)
    for _ in range(WARMUPS):
        execute(uc, registers, mode, observed)
    samples = [execute(uc, registers, mode, observed)
               for _ in range(REPEATS)]
    ordered = sorted(samples)
    p50 = statistics.median(ordered)
    p95 = ordered[math.ceil(0.95 * len(ordered)) - 1]
    check(math.isfinite(p50) and math.isfinite(p95) and
          0 < p50 <= p95, "invalid measured latency distribution")
    check(cold_ns > 0, "missing first execution")
    print("C1 MEDIATED CPU MODE PASS: " + mode)
    print(f"{mode}.first_run_ns={cold_ns}")
    print(f"{mode}.warm_p50_ns={p50:.0f}")
    print(f"{mode}.warm_p95_ns={p95}")
    print(f"{mode}.warm_instructions_per_s={STEPS * 1e9 / p50:.0f}")
    print(f"{mode}.samples={REPEATS}")
    return p50


def main():
    from unicorn import __version__
    check(__version__ == "2.1.4", "unreviewed Unicorn engine")
    check(hashlib.sha256(PROGRAM).hexdigest() == PROGRAM_SHA256,
          "source-owned workload hash mismatch")
    negatives()
    modes = {mode: measurement(mode) for mode in MODES}
    # Record ratios as data, NOT fixed pass/fail thresholds or predictions.
    print("C1 MEDIATED X86-64 EXECUTION MODE EVALUATION PASS")
    print("fixture_sha256=" + PROGRAM_SHA256)
    print(f"source_owned_instructions={STEPS}")
    print("engine=unicorn_2.1.4_research_only")
    print("same_guest_cpu_result=3_execution_modes")
    print(f"warmups={WARMUPS}")
    print(f"runs_per_mode={REPEATS}")
    print(f"single_step_to_batch_p50_ratio={modes['single_step'] / modes['batch']:.3f}")
    print(f"code_hook_to_batch_p50_ratio={modes['code_hook'] / modes['batch']:.3f}")
    print("host_system=" + platform.system())
    print("host_machine=" + platform.machine())
    print("host_python=" + platform.python_version())
    print("host_cpu_count=" + str(os.cpu_count()))
    print("benchmark_scope=ci_host_execution_only_not_ps5_game_performance")
    print("source_owned_guest_code_native_execution=none")
    print("production_backend_selected=no")


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, ValueError, RuntimeError) as ex:
        print(f"C1 MEDIATED EXECUTION EVALUATION REFUSED: {ex}",
              file=sys.stderr)
        sys.exit(1)
