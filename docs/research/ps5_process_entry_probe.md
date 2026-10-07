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
fs_base            : u64
gs_base            : u64
process_window_base: u64
process_window     : bytes
```

Raw values are evidence. Zero is a valid observation.

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
fs_base_nonzero
gs_base_nonzero
```

These fields may be compared across equivalent cases without assuming raw
addresses are stable.

## Repeat-run rule

At least two consecutive same-case observations should have the same structural
projection before a structural claim is promoted.

Structural equality is necessary, not sufficient, for an exact ABI rule.
Review the raw observations and provenance before promoting:

- a mandatory RSI role/value;
- an exact RSP layout/alignment;
- an FS/GS or TCB layout;
- any other raw address relationship.

Do not rewrite changing raw pointers into fixed values merely to make two runs
compare equal.

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
