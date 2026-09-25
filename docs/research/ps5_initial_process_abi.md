# PS5 initial-process ABI research

**Status:** active evidence record for #300  
**Last reviewed:** 2026-09-24

## Question

What exact state does the normal PS5 title loader establish at a title's ELF
entry point, and what minimum subset must Astraea reproduce before a retail
image may execute its first native instruction?

The production Linux diagnostic currently stops an otherwise-ready PS5/SCE
image at `unsupported_initial_process_abi`. This stop must not be bypassed by
reusing Astraea's synthetic owned-probe stack.

## Evidence classes

- **Controlled observation:** strongest for the exact selected title/profile.
- **Public hardware/toolchain observation:** useful PS5-specific evidence, but
  provenance/lineage matters.
- **Comparative emulator/PS4 behavior:** architecture context only.
- **Hypothesis:** never sufficient for guest-visible success.

## Public source: ps5link-sdk real-title CRT

Repository: https://github.com/Rufidj/ps5link-sdk  
Pinned commit: `ea771e535378740b6a058b8e5419eb8a0e0e0ec8`  
File: `linker/crt1.S`

The repository explicitly distinguishes installed/home-screen PS5 titles from
payload/ELF-loader programs. The startup file describes itself as the real
process entry point for a title launched by the console's own module loader.

Observed startup behavior at this revision:

1. `RDI` is treated as a loader-provided parameter-block pointer.
2. The first 32-bit value at `[RDI]` is read as `argc`.
3. `argv` is formed as `RDI + 8`.
4. `RSI` is saved as a loader-provided teardown routine.
5. The original `RDI` parameter-block pointer is passed unchanged to the
   imported libc `_init_env`.
6. The saved `RSI` teardown routine is registered through imported libc
   `atexit`.
7. The module's own `_fini` is also registered through `atexit`.
8. The startup then calls `main(argc, argv, 0)`, passes its return value to
   imported libc `exit`, and uses `UD2` as an unreachable terminal fallback.

This is directly useful evidence for the roles assigned by that startup object.

## Lineage limitation

The same file says its sequence is ported from SharpProspero's CRT emitter.

Therefore:

- ps5link and SharpProspero are **one evidence lineage** for this startup
  sequence;
- agreement between them is not independent corroboration;
- the behavior does not by itself establish the complete retail loader ABI.

The ps5link README also states that its current toolchain does not support TLS.
That is a reason to keep TLS as an unresolved boundary, not evidence that normal
retail PS5 processes lack TLS.

## Current supported claims

### Medium confidence / PS5 real-title toolchain evidence

For at least the tested ps5link title profile:

- `RDI` is a loader parameter-block pointer consumed by CRT/libc startup;
- `RSI` carries a loader-provided teardown routine used by CRT;
- the parameter block begins with an argc-like 32-bit field;
- argv-like pointer slots begin at +8 for that CRT;
- libc environment initialization consumes the original parameter-block
  pointer.

These claims are useful for designing probes and a future typed profile.

They are **not yet sufficient** to enable arbitrary retail native entry.

## Unknown load-bearing fields

### C1A — entry registers / parameter block

- total parameter-block size;
- exact pointer/value fields after the argv region;
- ownership and lifetime;
- environment representation;
- auxiliary-vector-like fields, if any;
- required initial values for registers other than evidenced RDI/RSI;
- exact entry `RSP` contents/alignment before title CRT mutation.

### C1B — process metadata

- relation between the entry parameter block and PS5 process/procparam
  structures;
- required process/module metadata initialization before entry;
- SDK/firmware version variation.

### C1C — primary-thread TLS/TCB

- TLS allocation algorithm;
- initial TLS image placement;
- TCB layout;
- initial `FS` and/or `GS` base;
- TLS module index/dynamic TLS behavior;
- interaction with system libc startup.

### C1D — bootstrap ordering

- which system/runtime modules must be loaded before entry;
- which relocations/imports must be resolved by the loader;
- constructor/init ordering;
- teardown ownership;
- initial thread/process services guaranteed to exist.

## Falsification rules

Do not promote a field when:

- it is present only in a payload/exploit loader contract;
- it is inferred from PS4 behavior without PS5 evidence;
- two public PS5 sources share the same implementation lineage;
- a title-specific CRT constructs the state itself rather than observing it
  from the loader;
- a candidate value is plausible but not observed;
- a result changes across repeated controlled runs without explanation.

## Smallest controlled observation

Preferred owned native-title probe:

1. use an independently generated title/profile on hardware the contributor is
   authorized to use;
2. replace normal CRT entry with the smallest owned `_start`;
3. before calls or stack-heavy mutation, record:
   - GPRs;
   - `RSP` and a bounded stack window;
   - `RDI` and a bounded parameter-block window;
   - `FS`/`GS` bases if safely observable;
   - relevant process/procparam pointers known from public metadata;
4. emit only normalized numeric/structural observations;
5. repeat the same build/run at least twice;
6. require stable observations for any field promoted;
7. compare only the evidenced RDI/RSI/argc/argv roles against the pinned
   ps5link CRT.

Do not commit:

- title binaries;
- system modules;
- firmware;
- keys;
- proprietary SDK output;
- decrypted platform content.

## Proposed promotion sequence

### P1 — entry envelope

Promote only:

- evidenced entry register roles;
- bounded parameter-block envelope;
- stack alignment/range requirements that are actually observed.

Acceptance: a synthetic profile can reproduce these fields and reject unknown
required fields explicitly.

### P2 — process metadata

Add only the procparam/process relationships required by the selected title.

### P3 — primary-thread TLS

Add TLS/TCB/FS-GS state once independently evidenced.

### P4 — bootstrap handoff

Add module/runtime initialization ordering required before executing the
selected title entry.

## Implementation boundary

When evidence is sufficient, introduce a typed process-entry profile/request
rather than embedding values directly in the Linux runner.

Conceptually:

```text
GuestImage
 + resolved runtime/module state
 + typed process-entry profile
 + primary-thread/TLS state
 -> validated GuestCpuContext + initial guest memory
 -> supervised Linux seccomp native entry
```

The process-entry builder should remain independent of:

- host Linux process startup;
- the synthetic owned-probe stack;
- Vulkan;
- title-specific patches.

## Comparative sources

PS4 emulator startup/TLS logic may identify useful questions but is not PS5
evidence. In particular, mature projects such as shadPS4 demonstrate that
primary-thread TLS, module linking, and process initialization become major
runtime subsystems under real games; their PS4 constants/ABI must not be copied
into Astraea.

## Next research action

Obtain a second independent public or controlled observation for C1A.

Until then, keep production retail diagnostics stopping at
`unsupported_initial_process_abi`.
