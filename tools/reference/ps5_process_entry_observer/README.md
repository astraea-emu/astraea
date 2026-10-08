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

The same pinned build bootstraps public `ps5-payload-dev/sdk` release v0.42,
whose libkernel stub set exports `sceKernelGetProcParam`. The post-init C1B
observation therefore uses the public stub family already consumed by the
clean-room title build; it does not depend on a private runtime symbol.

Astraea does not vendor that project. Keep the hardware experiment in a
separate local checkout.

### Preferred pinned-checkout preparation

Use a fresh local checkout of the reviewed external project at:

```text
2f672d1c2f508e26f82ce6e27cef289a0861413c
```

Then run, from the Astraea checkout:

```sh
python3 tools/reference/ps5_process_entry_observer/prepare_blackbear_checkout.py \
  --checkout /path/to/ps5-native-app-boilerplate
```

The script performs **no network or console operation**. It fails closed unless
the external checkout is at the pinned revision, is clean, and still contains
the exact reviewed build-script anchors. It then:

- copies Astraea's observer assembly/capture/emitter into that local checkout;
- compiles the ordinary project-owned CRT with `_start` renamed;
- compiles the observer as preprocessed x86-64 assembly and makes it the
  linked `_start`;
- adds a small constructor hook that emits one record after the normal CRT
  has completed `_init_env` and entered constructor processing;
- leaves `build/llvm-pie.elf` and `build/eboot.elf` as the exact offline
  analyzer inputs.

The external checkout is intentionally left dirty after preparation so its
mutation is visible. Do not commit that prepared checkout unless you are
deliberately maintaining a separate attributed research fork.

Preparation also writes `.astraea-ps5-entry-observer-v0` in the external
checkout. Preserve that file with the experiment record. It contains:

- the pinned upstream revision;
- the exact Astraea Git revision whose preparation/observer sources were used;
- SHA-256 digests of `entry.S`, `capture.h`, and `emit_observation.cpp`.

The preparation script refuses locally modified/untracked Astraea observer or
preparation sources, so those identifiers correspond to the files actually
copied into the external checkout.

### Exact build step after preparation

At the pinned external revision, `app` is the canonical normal folder-build
target and the Makefile default. After Astraea preparation succeeds, change
into the **prepared external checkout** and run:

```sh
make app
```

Do not run `make clean`, re-run project initialization, or rebuild from a
different source revision between hardware run 1 and run 2.

The pinned build may fetch/verify its declared public build dependencies on
first use; that network/bootstrap behavior belongs to the external project and
is intentionally separate from Astraea's offline preparation helper.

Before deployment/run 1, preserve locally:

```text
.astraea-ps5-entry-observer-v0
build/llvm-pie.elf
build/eboot.elf
dist/<TITLE_ID>/eboot.bin
```

Record SHA-256 for the three binary artifacts. Run 2 must use the same
`dist/<TITLE_ID>/eboot.bin` bytes and the same intermediate/final ELF pair used
by the offline analyzer.

The final signed `eboot.bin` is the deployment artifact. The analyzer inputs
remain the unsigned/intermediate `build/llvm-pie.elf` and final converted
`build/eboot.elf` exactly as emitted by that same build.

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

A null `sceKernelGetProcParam()` return is valid evidence. In that case the
emitter records `procparam_runtime=0` and `procparam_prefix=unavailable`;
the analyzer preserves the null result and reports both identity checks false
rather than treating the run as malformed.

The analyzer also derives the same normalized C1A structural projection used
by Astraea core from the frozen entry record:

- argc;
- argv[0] zero/non-zero;
- RSI zero/non-zero;
- RBP zero/non-zero;
- RSP modulo 16.

After capturing two same-artifact runs, compare them mechanically:

```sh
python3 tools/reference/ps5_process_entry_observer/procparam_identity.py \
  --intermediate /path/to/build/llvm-pie.elf \
  --final /path/to/build/eboot.elf \
  --compare-log-files /path/to/run1.log /path/to/run2.log
```

The comparison intentionally excludes raw runtime addresses and load bias. It
compares, in fixed order:

- argc;
- argv[0] zero/non-zero;
- RSI zero/non-zero;
- RBP zero/non-zero;
- RSP modulo 16;
- procparam API null/non-null state;
- procparam pointer identity;
- procparam prefix availability/identity;
- startup-vector vs procparam separation.

The JSON result reports `equivalent` plus the first structural difference and
both values. A matching comparison is necessary, not sufficient, for promotion:
retain and review both raw records before turning an exact relationship into a
PS5 rule.

This lets each run produce one offline JSON containing both C1A and C1B facts.

The analyzer reports, separately:

- derived load bias;
- expected mapped `PT_SCE_PROCPARAM` address;
- whether the API return is non-zero;
- observed API-return address;
- `pointer_match`;
- whether a prefix was available;
- static and observed 16-byte prefixes;
- `prefix_match`;
- when the entry observation is present, whether the loader-built startup
  pointer remains distinct from the API-return pointer.

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

## Optional C1C FS/GS sidecar evidence

Do not modify the title entry shim to execute `RDFSBASE` or `RDGSBASE` unless
that userspace mechanism is independently established for the selected
environment.

When an authorized external debugger can freeze/read the same **primary title
thread at the pre-CRT boundary**, record initial FS/GS bases externally and pass
them to the offline analyzer. The debugger observation must correspond to the
loader entry / observer snapshot before handoff into the ordinary CRT. A value
read later from `main` or after `_init_env` is not C1C entry evidence.

Astraea does not depend on the debugger transport. If the external environment
cannot stop the correct thread at that boundary, leave FS/GS unknown rather
than substituting a later value.

Single-run example:

```sh
python3 tools/reference/ps5_process_entry_observer/procparam_identity.py \
  --intermediate /path/to/build/llvm-pie.elf \
  --final /path/to/build/eboot.elf \
  --log-file /path/to/run1.log \
  --fs-base 0x... \
  --gs-base 0x...
```

Two-run structural comparison:

```sh
python3 tools/reference/ps5_process_entry_observer/procparam_identity.py \
  --intermediate /path/to/build/llvm-pie.elf \
  --final /path/to/build/eboot.elf \
  --compare-log-files run1.log run2.log \
  --compare-fs-bases 0x... 0x... \
  --compare-gs-bases unknown unknown
```

`unknown`, observed zero, and observed nonzero are distinct states. Raw FS/GS
addresses are retained in each run's output but excluded from repeat equality;
only the optional zero/nonzero state is compared.

Public experiment-design references include FreeBSD's debugger-visible
`PT_GETFSBASE`/`PT_GETGSBASE` requests and public PS5 debugging tooling with
a dedicated external FS/GS-base read. These justify an external observation
route only; they do not establish any PS5 entry value or make debugger
transport part of Astraea.

## Provenance boundary

Do not commit:

- console transport/deployment tooling;
- firmware or keys;
- Sony modules;
- proprietary SDK output;
- retail binaries;
- captured proprietary memory.

The hardware adapter and deployment mechanism remain outside Astraea core.
