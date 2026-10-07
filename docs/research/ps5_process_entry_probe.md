# PS5 process-entry reference observation

**Issue:** #307  
**Probe:** `astraea.ps5.process-entry`  
**Probe version:** 0  
**Format:** AstraeaProbe v0

## Purpose

This probe is the transport-neutral evidence boundary for C1 process-entry
research. Astraea core consumes an observation produced elsewhere; it does not
define how a reference PS5 is modified, reached, or asked to run code.

The selected hardware-side artifact must be an owned, independently generated
native title. Do not use a retail executable, proprietary SDK object, Sony
system module, firmware image, key, or decrypted platform content as the probe
artifact.

## Required v0 observations

The first validator slice consumes:

- entry `RDI`, `RSI`, `RBP`, `RSP`;
- initial `FS` and `GS` base when the external observer can capture them;
- base guest address of a bounded process-parameter window;
- the process-parameter bytes, including at least the independently
  corroborated 16-byte direct-title prefix.

Recommended AstraeaProbe result names:

```text
rdi                : u64
rsi                : u64
rbp                : u64
rsp                : u64
fs_base            : u64   # omit when not captured
gs_base            : u64   # omit when not captured
process_window_base: u64
process_window     : bytes
```

Raw values are evidence. Zero is a valid **observed** value. When FS or GS
base is not captured, omit that observation rather than encoding an invented
zero. Astraea preserves unavailable, observed-zero, and observed-nonzero as
three distinct states.

## Validation

The core validator requires the captured process-window base to equal RDI and
then applies the typed direct-title prefix validator.

It derives a deliberately small structural projection:

```text
argc
argv0_nonzero
rsi_nonzero
rbp_zero
rsp_mod16
fs_base_nonzero   # optional: unknown when FS base was not captured
gs_base_nonzero   # optional: unknown when GS base was not captured
```

These fields may be compared across equivalent cases without assuming raw
addresses are stable.

## Repeat-run rule

At least two consecutive same-case observations should have the same structural
projection before a structural claim is promoted.

The repeat comparator reports the first differing field plus both normalized
values. Boolean states are reported as 0/1. For optional FS/GS observations,
an unavailable capture remains absent rather than being collapsed into an
observed zero.

Structural equality is necessary, not sufficient, for an exact ABI rule.
Review the raw observations and provenance before promoting:

- a mandatory RSI role/value;
- an exact RSP layout/alignment;
- an FS/GS or TCB layout;
- any other raw address relationship.

Do not rewrite changing raw pointers into fixed values merely to make two runs
compare equal.

## Recommended owned-title observer

The first hardware observer should measure the entry state **before** a compiler
or CRT changes it.

Use a tiny project-owned assembly entry shim ahead of the normal `_start`.
The shim should perform no calls, pushes, stack alignment, dynamic allocation,
or host/console I/O before freezing the observation.

At minimum, use RIP-relative stores into a fixed title-owned capture record to
save:

- original `RDI`;
- original `RSI`;
- original `RBP`;
- original `RSP`;
- exactly the first 16 bytes at the original RDI process-block pointer.

After the snapshot is complete, transfer to the ordinary project-owned CRT
with the original RDI/RSI values. Once normal runtime initialization is safe,
serialize the frozen fields with a stable numeric format and emit them through
a normal application logging API such as `sceKernelDebugOutText`. A hardware
test harness may capture that kernel log externally and convert the record into
the AstraeaProbe observation described above.

This ordering matters: observing RSP from a C/C++ `_start` would measure the
compiler's own prologue rather than the loader boundary.

### FS/GS follow-up

Do not add `RDFSBASE`/`RDGSBASE` to the first observer merely because the Zen 2
CPU supports those instructions. Whether userspace execution of those
instructions is enabled is itself a platform fact. Treat FS/GS-base capture as
a separate controlled probe using an independently evidenced safe mechanism.
Until then, the v0 Astraea observation should omit those fields when they are
not captured. It must never encode "unknown" as zero or turn either state into
a PS5 rule.

### Repetition and provenance

For promotion-quality evidence:

1. build one exact owned-title candidate from a clean commit;
2. record the artifact SHA-256, toolchain commit, firmware and launcher/loader
   environment that are lawfully known;
3. execute the same candidate twice without changing inputs;
4. preserve the complete raw log records from both runs outside Astraea core;
5. feed the normalized fields into this validator;
6. require matching structural projections before considering any new rule;
7. review raw values/relationships separately before promoting an exact ABI
   field.

## External adapter boundary

The hardware adapter remains outside Astraea core. It may emit AstraeaProbe v0
JSON or another transport that is converted into the same typed observation.

Astraea core must not import or require exploit frameworks, jailbreak clients,
firmware keys, proprietary system modules, authentication bypass code, or a
console RPC implementation.

## Next evidence after this validator

Use an owned custom entry stub that captures state before ordinary CRT mutation.
The highest-value unresolved facts are:

1. whether RSI is non-zero/stable and what teardown ownership it represents;
2. exact initial RSP residue and bounded stack structure;
3. initial FS/GS and primary-thread TCB/TLS relationships;
4. which bootstrap effects are required before title entry.

The production retail diagnostic remains at
`unsupported_initial_process_abi` until the selected profile has all required
contracts.
