# C1 — Mediated CPU execution mode evaluation

**Status:** source-owned, Linux x86-64 GitHub Actions research measurement.
Not a PS5 benchmark, production engine choice, game-frame measurement, or
claim that any emulator backend has acceptable gameplay performance.

## Same program, three execution modes

`benchmark_unicorn_owned_execution_modes.py` fixes and SHA-256 pins one
Astraea-authored 1,024-byte executable guest code page payload: **256
consecutive `SUB RAX,1` instructions** (`48 83 e8 01`), RAX initially
256 and synthetic RFLAGS=2. It evaluates the same exact source bytes using
the already pinned, external Unicorn 2.1.4 Python wheel in:

- **batch:** one bounded `emu_start(..., count=256)` per run;
- **code_hook:** the same bounded call with an instruction notification for
  every expected guest RIP and instruction width;
- **single_step:** 256 individual `emu_start(..., count=1)` calls with a
  checked continuous guest RIP before each.

All three must reach the same exact guest RIP, register file, arithmetic
flags and unchanged executable bytes. They do not model stack, memory
effects, faults, threads or Sony calls. `UC_HOOK_CODE` notifications are
*pre-execution observations*, not retirement or successful-write proofs.

The benchmark additionally requires a rejected zero/over-limit budget,
refuses an incomplete 255-step execution as a full result, and rejects a
mutated final immediate when compared against the original CPU expectation.

## Timing methodology

For each mode, instantiate a fresh virtual engine with identical memory and
CPU context. Record first execution time after allocation/mapping, then
perform **four untimed warmups and 25 timed repeated executions** after
resetting the same guest CPU context. Measure time with
`perf_counter_ns` around the execution mechanism. Output first execution
latency, warm median and nearest-rank p95, instructions/s computed from the
median, measured batch-relative ratios, platform and Python/CPU-count
metadata. No hard-coded performance threshold affects compatibility gating.

This compares **Python-mediated API usage strategies on a shared CI runner**.
Single-step mode includes 256 host/Python calls, while batch mode makes one;
hook mode instruments each guest instruction. Results are *not* normalized
across architectures, process scheduling, CPU clock, thermal state, VM noise
or actual PS5 code. Repeated execution reuses translated code after warmup
and excludes engine setup from timed samples. No guest GPU, PPU, audio,
kernel, memory-map changing or API-call workload is represented.

## Interpretation and next gate

- If single-step overhead is substantial, do not build production execution
  around a per-instruction Python roundtrip merely to obtain trace parity.
  Keep per-instruction tracing as a bounded diagnostic/reference mode.
- Batch execution is only a candidate fast path. It does **not** prove
  enforceable memory-write authorization, fine-grained fault recovery,
  bounded external work or acceptable performance on real workloads.
- Next: attach a controlled source-owned **memory-reading/writing** workload,
  compare guest state/effects and fault behavior with Astraea's checked
  ledger, test an appropriate backend callback or memory-broker boundary,
  measure on relevant host OS/architectures, then review legal provenance.
- Original source-pinned Breakout still stops before its first unresolved
  imported CALL at RIP `0x23c`. Native outside-authored guest admission
  remains blocked by #410. No license to vendor Unicorn is implied.

This is one decision input under [proposed ADR 0013](../adr/0013-cpu-backend-trace-evaluation.md)
and [backend acceptance issue #461](https://github.com/astraea-emu/astraea/issues/461).
Numeric results belong to each GitHub Actions job log, not a timeless
project-compatibility statement.
