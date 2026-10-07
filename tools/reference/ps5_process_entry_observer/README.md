# PS5 process-entry observer v0

This directory contains the Astraea-owned pre-CRT observer used to answer the
remaining C1A process-entry questions without changing Astraea's production
retail admission policy.

It is **not** a console transport, exploit, jailbreak, loader, or firmware
tool. It is a tiny x86-64 entry shim intended to be linked into an independently
generated native title in an execution environment the contributor is
authorized to use.

## What v0 records

The fixed `AstraeaPs5EntryCaptureV0` record contains:

- all integer GPRs visible at the shim entry;
- exact original RSP;
- the first 16 bytes at original RDI;
- a version field;
- a completion magic published only after the bounded snapshot is complete.

v0 intentionally does **not** read a speculative stack window and does not try
to read FS/GS base. Those remain separate evidence questions.

## Preservation invariant

Before the snapshot is complete, the shim uses only MOV-family instructions.
Those instructions do not modify RFLAGS.

The shim uses R10 as its only scratch GPR after first saving it, restores R10,
and then tail-jumps to the normal CRT. RDI, RSI, RBP, RSP, the remaining GPRs,
and flags are therefore handed to the CRT unchanged by the observer itself.

The capture writes title-owned writable storage. The first 16 bytes at RDI are
read because the validated native-title CRT already consumes that exact prefix.

## Record completion

`magic` is written last.

The completed little-endian magic bytes are `ASTRPEV0`.

Consumers must ignore a record whose magic or version is wrong.

## Current external integration reference

The integration reviewed on 2026-10-07 is:

```text
blackbearreloaded/ps5-native-app-boilerplate
commit 2f672d1c2f508e26f82ce6e27cef289a0861413c
```

At that revision:

- `tooling/native/app_crt.cpp` defines the ordinary project-owned `_start`;
- the build links with `-e _start`;
- the project is GPL-3.0-or-later;
- its clean-room runtime/startup artifact is documented as hardware-validated
  on PS5 firmware 6.02 and 12.70.

Astraea does not vendor that project. Keep the hardware experiment in a
separate local checkout.

### Preferred preparation helper

For the pinned BlackBear revision, Astraea provides a fail-closed local
preparation helper. It performs no clone/download/deployment operation.

With a clean checkout at exactly the pinned commit, preview first:

```sh
python3 tools/reference/ps5_process_entry_observer/prepare_blackbear_checkout.py \
  --check /path/to/ps5-native-app-boilerplate
```

Then apply the exact local integration:

```sh
python3 tools/reference/ps5_process_entry_observer/prepare_blackbear_checkout.py \
  /path/to/ps5-native-app-boilerplate
```

The helper refuses a wrong revision, dirty worktree, or unexpected upstream
source text. It does not commit/reset the external checkout. It copies the
three Astraea-owned observer sources into `.astraea-observer/`, makes only the
pinned build/main transformations described below, and writes a provenance
manifest with upstream/Astraea identities and file hashes.

After preparation, use the external project's ordinary build flow. Console
deployment/launch remains outside Astraea and must use an environment the
contributor is already authorized to operate.

### Required build delta

For one controlled experiment:

1. compile the upstream project-owned `tooling/native/app_crt.cpp` with

   ```text
   -D_start=astraea_reference_crt_start
   ```

   so the normal CRT body is retained under the handoff symbol expected by
   `entry.S`;

2. compile Astraea's `entry.S` with the same x86-64 target toolchain and

   ```text
   -DASTRAEA_PS5_ENTRY_SYMBOL=_start
   ```

3. link the observer object before/alongside the renamed CRT object while
   retaining the upstream `-e _start` entry selection;

4. include `capture.h` in owned application code that runs **after** ordinary
   runtime initialization and serialize the completed record through a normal
   application logging path already supported by that environment.

Do not add logging, calls, stack adjustment, constructors, or C/C++ code before
the assembly snapshot.

## Stable log content

The post-init logger should preserve raw values from the record, at minimum:

```text
rdi
rsi
rbp
rsp
process_prefix[16]
```

It may additionally preserve the other captured GPRs.

For Astraea promotion, the external adapter converts those values to the
`astraea.ps5.process-entry/v0` observation contract documented in
`docs/research/ps5_process_entry_probe.md`.

Do not normalize raw addresses into invented fixed values. The Astraea
validator derives the structural comparison separately.

## Repeat rule

Use one exact owned-title artifact and run it at least twice without changing
the build or semantic environment.

Record, where lawfully known:

- native-title source commit;
- final artifact SHA-256;
- observer commit;
- toolchain commit;
- firmware/platform version;
- launcher/loader environment.

The two runs must produce matching structural projections before any new ABI
rule is considered. Exact raw relationships still require review.

## Same-run C1B procparam observation

Do not schedule a second hardware cycle for #312.

After the normal CRT/runtime is initialized, the owned application should
also record:

- runtime address of `astraea_ps5_entry_capture_v0`;
- pointer returned by `sceKernelGetProcParam()`;
- exactly the first 16 bytes at that returned pointer.

The pinned converter preserves allocated input-section virtual addresses when
building the final PS5 executable and adds the static `PT_SCE_PROCPARAM`
program header separately. That lets the host derive the title load bias from
the observed capture-object address and its intermediate-PIE symbol value.

Preferred path: call `astraea_emit_ps5_entry_observation_v0()` once
near the beginning of the owned application's normal `main`, capture that
single kernel-log line to a local text file, then run:

```sh
python3 tools/reference/ps5_process_entry_observer/procparam_identity.py \
  --intermediate /path/to/build/llvm-pie.elf \
  --final /path/to/build/eboot.elf \
  --log-file /path/to/run1.log
```

The emitter performs no allocation and formats the C1A/C1B values itself after
normal runtime initialization. Manual `--capture-runtime`,
`--api-procparam-runtime`, and `--api-procparam-prefix` arguments remain
available for independent adapters, but must not be mixed with `--log-file`.

The analyzer reports, separately:

- derived load bias;
- expected mapped `PT_SCE_PROCPARAM` address;
- observed API-return address;
- `pointer_match`;
- static and observed 16-byte prefixes;
- `prefix_match`.

A mismatch is a valid experimental result, not an analyzer failure.

The analyzer accepts only ELF64 little-endian x86-64 inputs, requires a unique
defined observer capture symbol and a unique `PT_SCE_PROCPARAM`, validates the
owned static procparam size/`ORBI` prefix, and uses checked u64 arithmetic.

## Relationship to C1B

After the normal runtime initializes, the same owned title may perform the
separate #312 observation of `sceKernelGetProcParam()` and compare it with the
mapped title-owned `PT_SCE_PROCPARAM`.

Do not merge the startup-vector and static-procparam concepts.

## Tests

On Linux x86-64, Astraea's host test compiles this exact assembly source under
its non-entry default symbol, drives it with a synthetic register fixture, and
proves:

- capture layout and magic;
- first-16-byte copy;
- exact RSP preservation across the tail jump;
- every captured GPR reaching the synthetic CRT target unchanged.

Other CI platforms compile the portable record-layout test but do not assemble
or execute the x86-64 shim.

## Provenance boundary

Do not commit:

- console transport/deployment tooling;
- firmware or keys;
- Sony modules;
- proprietary SDK output;
- retail binaries;
- captured proprietary memory.

The hardware adapter and deployment mechanism remain outside Astraea core.
