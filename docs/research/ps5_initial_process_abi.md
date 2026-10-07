# PS5 initial-process ABI research

**Status:** active evidence record for #300  
**Last reviewed:** 2026-10-07

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

### Corroborated partial C1A direct-title prefix

Two distinct hardware-exercised native-title startup lineages now support this
subset:

- `RDI` / the first SysV argument is a loader-provided process-parameter
  block;
- a 32-bit argc-like field is consumed at byte offset 0;
- the argv-like pointer vector begins at byte offset 8;
- the original process-block pointer is passed unchanged to runtime environment
  initialization.

The original lineage is `Rufidj/ps5link-sdk` / SharpProspero.

The second lineage is
`blackbearreloaded/ps5-native-app-boilerplate`. Its current
`docs/RUNTIME_SHIM.md` states that startup behavior and the clean-room runtime
were independently authored, and that the exact generated title/runtime was
hardware-validated on PS5 firmware 6.02 and 12.70. Its project-owned
`tooling/native/app_crt.cpp` consumes the same process-block/argc/argv prefix.

This is sufficient to promote the **prefix shape** into a typed partial C1A
profile. It is still not sufficient to execute arbitrary retail code.

### Still unresolved at C1A

`RSI` remains deliberately unresolved as a required loader contract.
The ps5link lineage treats it as a loader teardown callback. The independently
authored BlackBear CRT accepts the same second argument and conditionally
registers it, but its published hardware-validation record does not establish
that a non-null callback was supplied or invoked.

Exact initial `RSP` contents/alignment and any additional required register
values also remain unresolved.

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

## 2026-09-25 public-source sweep

A follow-up review after C0 completion re-checked the normal-title startup
evidence and current public emulator/toolchain projects.

### Re-verified primary lineage

The pinned `Rufidj/ps5link-sdk@ea771e5` startup source still directly
implements the observed C1A roles used above:

- `(%rdi)` is consumed as an argc-like 32-bit value;
- `rdi + 8` is used as the argv-like vector;
- `rsi` is preserved as the loader-provided teardown routine;
- the original `rdi` is passed unchanged to `_init_env`.

The repository also explicitly states that this startup sequence is ported from
SharpProspero. Treat those two projects as one lineage for this behavior, not
as independent corroboration.

Sources:

- https://github.com/Rufidj/ps5link-sdk/blob/ea771e535378740b6a058b8e5419eb8a0e0e0ec8/linker/crt1.S
- https://github.com/Rufidj/ps5link-sdk/tree/ea771e535378740b6a058b8e5419eb8a0e0e0ec8

### Independent-source result

The reviewed public sources did not identify a second independent
**normal-title loader-entry observation** that establishes the same RDI/RSI,
stack, parameter-block, and TLS state.

Payload/ELF-loader SDK entry contracts were deliberately excluded because they
describe a different loader contract. PS4 emulator startup/TLS behavior and
other PS5 emulator implementations remain useful comparative questions, but
they are not evidence for the PS5 retail loader's initial register values.

Therefore this sweep does **not** change the promotion threshold: do not enable
retail native entry from the single ps5link/SharpProspero lineage.

### Architecture cross-check

Current public emulator/compiler work continues to support the *shape* of
Astraea's roadmap without establishing C1 values:

- mature direct-title emulators encounter runtime linking, TLS, threading,
  synchronization, HLE, and resource-tracking pressure;
- current PS5 emulator projects separate guest GPU semantics from host Vulkan
  resource management and shader recompilation;
- Mesa RADV separates semantic/compiler IR lowering and optimization from the
  AMD machine-code backend.

These comparisons justify expecting later dependency classes, but none are a
substitute for PS5 process-entry evidence.

## 2026-10-07 independent hardware-exercised corroboration

Repository: `blackbearreloaded/ps5-native-app-boilerplate`.

The current clean-room runtime documentation states that BlackBearReloaded
designed and implemented the startup behavior independently and that the exact
generated native title/runtime artifact was hardware-validated on PS5 firmware
6.02 and 12.70.

The project-owned `_start` consumes:

- `process_parameters` as the first SysV argument;
- a 32-bit argc-like value at byte +0;
- argv beginning at byte +8;
- the original process pointer through `_init_env`.

This is independent hardware-exercised corroboration for the same direct-title
prefix exposed by the ps5link/SharpProspero lineage, so that prefix is now
promotable into typed Astraea code.

The source also accepts a second `loader_teardown` argument and conditionally
registers it, but the published validation does not prove that the loader
supplied a non-null callback or that it fired. Do not promote a mandatory RSI
value/role yet.

Provenance nuance: the same repository's executable converter contains
documented SharpProspero-derived portions. The clean-room runtime/startup
documentation separately attributes the startup behavior to BlackBearReloaded
as independently authored. Preserve both facts in provenance.

## 2026-10-07 bootstrap-boundary refinement

The October scene review adds an important bootstrap distinction alongside the
partial C1A prefix promotion above. It still does not promote any guessed PS5
constant, mandatory RSI value, stack contract, or TLS/TCB layout.

- Current KytyPS5 independently synthesizes an argc/argv-like title-entry
  block, teardown callback, and guest stack before calling a title entry.
  This converges on the broad ps5link RDI/RSI shape but is implementation
  behavior, not a real-loader observation.
- Current `mattias800/prosper` directly enters the title with a SysV-style
  initial vector, RDI pointing at that vector, RSI set to zero, and guest
  FS/TCB activated before the jump. This is independent implementation
  pressure, not controlled loader evidence; its success with real titles
  demonstrates why compatibility alone cannot establish the exact hardware
  entry contract.
- Current Force67/prosperity instead models PS5 startup by entering libkernel
  first with a FreeBSD-like initial stack and a non-zero initial FS/TCB.
  Its firmware-derived TCB details are research clues, not Astraea constants.

Therefore C1 must distinguish two boundaries:

- **C1-pre:** state/effects required at the first guest bootstrap instruction
  Astraea chooses to model or replace through HLE;
- **C1-title:** state required when the title's own entry is finally invoked.

Astraea may clean-room reproduce required bootstrap effects without executing
proprietary system modules, but only after those effects are evidenced. The
new independent title evidence is sufficient only for the direct-title prefix;
it is not sufficient to enable retail native entry.

See `docs/research/scene_review_2026-10-07.md`.

## Next research action

Encode the corroborated direct-title prefix as a typed partial C1A contract
without connecting it to retail native entry.

Then resolve the remaining load-bearing evidence in priority order:

1. observe whether RSI is non-null/stable and what teardown ownership it
   establishes;
2. capture exact initial RSP/alignment and a bounded stack window before CRT
   mutation;
3. capture initial FS/GS and primary-thread TLS/TCB requirements;
4. establish the minimum bootstrap effects that must exist before the selected
   title entry.

Production retail diagnostics must continue stopping at
`unsupported_initial_process_abi` until the selected profile has all required
contracts.
