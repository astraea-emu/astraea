# PS5 procparam runtime observation

**Issue:** #312  
**Status:** C1B controlled-observation contract  
**Selected-artifact scope:** one exact owned native title/build

## Question

For one exact native-title artifact, does `sceKernelGetProcParam()` return the
runtime mapping of that artifact's static `PT_SCE_PROCPARAM` program-header
object?

This is intentionally narrower than claiming one universal procparam ABI.

## Why startup RDI is not the static procparam

The independently corroborated direct-title startup block consumes:

- argc-like `u32` at byte +0;
- argv-like pointer vector beginning at byte +8.

The reviewed native-title generator emits a separate `PT_SCE_PROCPARAM` object
whose selected artifact has its own file bytes and image-relative virtual
address. These objects must remain separate.

## Artifact-side evidence

Astraea derives the following directly from the exact final PS5/SCE ELF:

```text
entry_image_virtual_address
procparam_image_virtual_address
procparam_prefix[16]
```

The offline `procparam_identity.py` analyzer requires exactly one final
`PT_SCE_PROCPARAM`, at least 16 file-backed bytes, an in-bounds prefix, and
unambiguous final `PT_LOAD` coverage for both the observer anchor and procparam.

The 16 bytes are treated as opaque selected-artifact evidence. The validator
does not hard-code `0x60`, `ORBI`, SDK numbers, or a firmware-global layout.

## Runtime observation

After the ordinary owned CRT/runtime has initialized safely, record:

```text
startup_parameters     # original pre-CRT RDI saved by the entry observer
entry_runtime_address  # runtime address of this exact title's observer _start
api_return             # sceKernelGetProcParam() return value
api_prefix[16]         # only when api_return != 0
```

A null API return is valid evidence and must not be dereferenced.

## ASLR-independent mapping test

The final native-title converter reviewed for the selected experiment preserves
the linked image entry virtual address when writing the final SCE ELF. The
runtime observer symbol is the selected ELF entry.

The offline analyzer therefore computes:

```text
load_bias = entry_runtime_address - entry_image_virtual_address
expected_mapped_procparam = load_bias + procparam_image_virtual_address
```

and reports independently:

- whether the API return is non-zero;
- whether it equals the expected mapped procparam address;
- whether its first 16 bytes equal the exact artifact procparam prefix;
- whether it remains distinct from the loader-built startup-parameter pointer.

Overflow and impossible negative load-bias relationships are typed failures.

## Same-run integration with C1A

Use the repository-owned observer in:

`tools/reference/ps5_process_entry_observer/`

One exact hardware run should produce both:

1. the pre-CRT `RDI`/`RSI`/`RBP`/`RSP` + startup-prefix capture;
2. after normal runtime initialization, the procparam observation above.

This prevents C1A and C1B from requiring separate artifacts or loader
environments.

## Repeat rule

Run the exact same artifact at least twice under the same semantic environment.

Before promoting the selected-profile relationship, require both runs to agree
on the structural facts:

- API return non-zero/null state;
- API-return == expected mapped procparam;
- API prefix == artifact prefix;
- startup vector distinct from API return.

Absolute runtime addresses may move under ASLR. Equality is evaluated only
after deriving the per-run load bias.

## What successful evidence would justify

For the exact selected profile, repeated successful observations can justify:

> `sceKernelGetProcParam()` exposes the mapped static `PT_SCE_PROCPARAM`
> object belonging to the main executable.

That would be sufficient to design the corresponding selected-profile HLE/
runtime relationship in C1/C2.

It would **not** by itself prove:

- every firmware/SDK uses the same procparam contents;
- the startup RDI block is a procparam pointer;
- a fixed absolute procparam address;
- the complete semantics of fields beyond the observed/independently supported
  subset.

## Provenance

For promotion-quality evidence retain outside Astraea core:

- exact title/toolchain commit;
- final artifact SHA-256;
- Astraea observer/validator commit;
- lawfully known firmware/platform version;
- launcher/loader environment;
- both complete raw observation records.

No retail binary, proprietary module, key, firmware image, or captured
proprietary memory should be committed to Astraea.
