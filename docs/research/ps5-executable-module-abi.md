# PS5 executable/module ABI evidence map

**Issue:** #8  
**Last verified:** 2026-09-21  
**Purpose:** identify which PS5 executable/module-loading facts are supported strongly enough to guide Astraea, while keeping unsupported behavior explicitly unknown.

## Scope

This note covers public evidence for the shape and identity of PS5 executable and
module ELF metadata: file types, program-header conventions, dynamic tags,
module/library identity, symbol identifiers, and relocation families.

It does **not** cover:

- Sony firmware, keys, proprietary SDK material, or copyrighted retail binaries.
- Decrypting or bypassing authentication of retail SELF/SPRX containers.
- Kernel exploitation, jailbreak procedures, or loader exploits.
- Guessing PS5 behavior from PS4 precedent when PS5 evidence is absent.

The implementation rule is: evidence may justify a parser/specification change;
it does not automatically justify HLE behavior.

## Evidence classes

- **Documented public source** — a public project documents behavior it implements
  or observes.
- **Cross-project convergence** — independent public projects encode the same
  constant/shape.
- **Hardware-backed community observation** — a public project explicitly states
  that a behavior or representation was checked against PS5 hardware/modules.
- **Hypothesis / unknown** — insufficient evidence for Astraea to encode behavior.

No public source listed here is an official Sony ABI specification.

## Sources pinned for this review

### SharpProspero

Repository: https://github.com/SvenGDK/SharpProspero  
Pinned commit: `9220876e25bc28aca1f65ea644783a479949ad77`

Relevant files:

- `tools/SharpProspero.Link/DynamicWriter.cs`
- `tools/SharpProspero.Link/NidEncoder.cs`
- `docs/modules.md`
- `docs/signed-and-unsigned.md`

Permalinks:

- https://github.com/SvenGDK/SharpProspero/blob/9220876e25bc28aca1f65ea644783a479949ad77/tools/SharpProspero.Link/DynamicWriter.cs
- https://github.com/SvenGDK/SharpProspero/blob/9220876e25bc28aca1f65ea644783a479949ad77/tools/SharpProspero.Link/NidEncoder.cs
- https://github.com/SvenGDK/SharpProspero/blob/9220876e25bc28aca1f65ea644783a479949ad77/docs/modules.md
- https://github.com/SvenGDK/SharpProspero/blob/9220876e25bc28aca1f65ea644783a479949ad77/docs/signed-and-unsigned.md

SharpProspero is current community tooling, not an official source. Its NID
encoder comments explicitly describe comparison with on-device module exports,
so those observations are stronger than code copied only from an older
implementation, but they still require independent confirmation before Astraea
treats them as platform law.

### Kyty

Repository: https://github.com/InoriRus/Kyty  
Pinned commit: `4733b7e1c91b10554a52007903d74dc76c39a230`

Relevant file:

- https://github.com/InoriRus/Kyty/blob/4733b7e1c91b10554a52007903d74dc76c39a230/source/emulator/include/Emulator/Loader/Elf.h

Kyty is useful as an independent cross-project encoding of ELF/SCE constants.
Its age and combined PS4/PS5 ancestry mean a constant appearing there is
corroboration, not proof that every variant is used by every PS5 firmware.

### PS5SDK

Repository: https://github.com/PS5Dev/PS5SDK  
Pinned commit: `a2e03a2a0231a3a3397fa6cd087a01ca6d04f273`

Relevant file:

- https://github.com/PS5Dev/PS5SDK/blob/a2e03a2a0231a3a3397fa6cd087a01ca6d04f273/README.md

This source is important mostly as a **negative boundary**: its ELF format and
entry contract target payloads loaded by a WebKit/ELF-loader path. Payload
`payload_main` arguments and its `dlsym` bootstrap must not be assumed to be
the normal installed-title/module ABI.

### ps5link-sdk

Repository: https://github.com/Rufidj/ps5link-sdk  
Pinned commit: `ea771e535378740b6a058b8e5419eb8a0e0e0ec8`

Relevant file:

- https://github.com/Rufidj/ps5link-sdk/blob/ea771e535378740b6a058b8e5419eb8a0e0e0ec8/README.md

ps5link is derived from SharpProspero's linker, so it is **not independent
evidence** for the format constants. Its value is that it reports hardware
execution of title-style output and therefore provides an additional
hardware-oriented sanity check on the overall representation.

## Evidence map

### 1. Base ELF profile

**Observation**

Public PS5-oriented implementations converge on:

- ELF64.
- little-endian data.
- x86-64 machine type.
- FreeBSD OS/ABI value 9 for module-style files.
- SCE-specific ELF file types:
  - `0xFE10` — dynamic executable / application-style executable.
  - `0xFE18` — dynamic/shared module.

Kyty defines both SCE file types directly. SharpProspero's current dynamic
writer emits the same values.

**Confidence:** high for parser recognition; medium for exactly which producer
or firmware uses which form.

**Astraea implication**

Astraea's generic ELF parser currently accepts ordinary `ET_EXEC` and
`ET_DYN`. Future PS5-profile parsing may add explicit support for the two
SCE-specific values, but only behind a PS5/SCE profile rather than silently
broadening generic ELF semantics.

### 2. PS5/SCE program-header vocabulary

**Observation**

Kyty exposes SCE-specific program-header values including:

- `0x61000000` — dynlib/dynamic-linking data.
- `0x61000001` — process-parameter data.
- `0x61000010` — relocation-read-only region.

SharpProspero's dynamic writer also models SCE process-parameter and RELRO
segments while producing title/module ELF output.

**Confidence:** medium-high as a format vocabulary; exact loader requirements
and ordering remain unverified in Astraea.

**Astraea implication**

Do not invent behavior for these program headers yet. First add typed
recognition plus preservation, then use synthetic fixtures and controlled
observations to determine required validation and mapping semantics.

### 3. SCE dynamic tags

**Observation**

Kyty and SharpProspero independently contain the same current-form tag values:

- `0x61000043` — module information.
- `0x61000045` — needed module.
- `0x61000047` — export library.
- `0x61000049` — import library.
- `0x61000041` — original filename.
- `0x61000011` — module attributes.
- `0x61000017` — export-library attributes.
- `0x61000019` — import-library attributes.
- `0x6100003d` — hash-table size.
- `0x6100003f` — symbol-table size.

Kyty also preserves older/alternate tag values for several records, for
example module-info, needed-module, import-library, export-library, and
original-filename variants.

**Confidence:** high that these values exist in the public SCE/Orbis format
family; medium that Astraea knows when each legacy/current variant is valid on
PS5.

**Astraea implication**

The next parser should:

- preserve raw tag/value/index exactly, as M1 already does;
- build a separate typed `SceDynamicMetadata` view rather than changing the
  generic dynamic parser;
- recognize both current and legacy forms as distinct evidence-bearing values;
- reject contradictory singleton records deterministically;
- avoid assigning semantic precedence between legacy/current forms until
  evidence establishes it.

### 4. Module identity and library identity are separate

**Observation**

SharpProspero documents and implements module and library identities as
different namespaces. A module may publish multiple libraries. Imports name
both a module and a library, and the two receive separate numeric identifiers.

Its module documentation also states that link-time imports carry module and
library version information and require matching versions.

**Confidence:** hardware-backed community observation; medium-high.

**Astraea implication**

Do **not** model a PS5 import as only:

`module-name + symbol`

The future identity should be at least structurally capable of:

`module + library + symbol-identifier + module-version + library-version`

Version-match policy, fallback behavior, and resolution precedence remain
unknown and should not be guessed.

### 5. Dynamic symbol identity uses an NID-bearing long form

**Observation**

SharpProspero reports module dynamic-string entries of the form:

`<nid>#<library-id>#<module-id>`

Its current linker writes imports and exports in this form. The first component
is an 11-character symbol identifier; the following components encode the
library and module identities.

**Confidence:** hardware-backed community observation, but not yet
independently reproduced by Astraea hardware experiments.

**Astraea implication**

Future parsing should split **representation** from **meaning**:

- parse and retain the raw dynamic-string spelling;
- optionally decode a structurally valid long form into
  `nid/library-id/module-id`;
- never substitute a guessed plain function name for an NID;
- plain-name catalogs are evidence databases, not loader truth.

### 6. NID computation

**Observation**

SharpProspero's current `NidEncoder` describes an 11-character identifier
derived from a salted SHA-1 digest, with byte-order transformation and a custom
64-character alphabet. Its comments say the result was checked against
on-device module exports for a set of common runtime functions.

ps5link reuses the SharpProspero linker/NID work and reports title-style output
executing on PS5 hardware, but because it is derived from SharpProspero it is
not an independent confirmation of the algorithm.

**Confidence:** medium-high community evidence; independent Astraea
confirmation still required before making this a compatibility guarantee.

**Astraea implication**

Do not implement NID generation merely because the algorithm is public. First
write a small pure function plus independently sourced test vectors. Later,
AstraeaProbe can compare computed identifiers against controlled exports on
owned hardware without storing proprietary modules in the repository.

### 7. Relocation families

**Observation**

Kyty's ELF definitions include the x86-64 relocation types:

- `R_X86_64_64` (1)
- `R_X86_64_GLOB_DAT` (6)
- `R_X86_64_JUMP_SLOT` (7)
- `R_X86_64_RELATIVE` (8)
- `R_X86_64_DTPMOD64` (16)

SharpProspero's dynamic writer emits or models the same core families,
including TLS module-index relocation.

**Confidence:** high that these are relevant format values; lower for complete
runtime relocation order and TLS semantics.

**Astraea implication**

M1 already parses relocation records generically. PS5 work should add
evidence-backed relocation **semantics** one type at a time, with synthetic
fixtures and deterministic failures for unsupported types.

### 8. Application/module ABI is not the payload ABI

**Observation**

PS5SDK's public README describes its payload entrypoint as `payload_main`
receiving loader-provided arguments and resolving functions through a
payload-loader/`dlsym` path. The project explicitly says this is primarily a
payload SDK rather than a complete application toolchain.

SharpProspero/ps5link instead model installed-title/module dynamic linking.

**Confidence:** high.

**Astraea implication**

Payload calling conventions, bootstrap arguments, import patching, and loader
behavior must not be copied into the installed-title execution path. Astraea
should keep a separate research label for:

- payload-loader ABI;
- application entry ABI;
- PRX/module ABI.

### 9. Signed container versus underlying ELF

**Observation**

SharpProspero distinguishes plain ELF/PRX representations from signed
SELF/SPRX containers and treats the container as wrapping an underlying ELF
module representation. Its public documentation also distinguishes readable
developer-style containers from sealed retail material.

**Confidence:** medium as community tooling behavior.

**Astraea implication**

For current clean-room work, the useful boundary is the underlying lawful ELF
representation. Container authentication/decryption is **not** required for the
next Astraea milestone and must not become a DRM-bypass project.

## What Astraea already has

The M1 loader already provides useful substrate:

- strict ELF64 parsing;
- validated program-header and `PT_LOAD` ranges;
- generic dynamic-entry preservation;
- standard dynamic string-table metadata;
- `DT_NEEDED` / SONAME representation;
- bounded dynamic symbols;
- REL/RELA/PLT relocation-table parsing;
- TLS template representation;
- initialized-image reads;
- typed deterministic failures.

This means the PS5-specific work should be an **additive typed metadata layer**,
not a replacement parser.

## Proposed implementation boundary after #8

The evidence currently justifies designing, but not yet merging all behavior
for, these types:

```text
SceElfProfile
SceDynamicMetadata
SceModuleIdentity
SceLibraryIdentity
SceSymbolIdentity { raw, nid, library_id, module_id }
SceImportIdentity { module, library, nid, versions }
```

A future parser slice should be data-only:

1. accept SCE file types only under an explicit PS5/SCE profile;
2. recognize/preserve SCE program-header and dynamic-tag values;
3. parse module/library/version records without resolving anything;
4. split NID-bearing long-form dynamic symbol names without mapping them to HLE;
5. preserve unknown records;
6. reject arithmetic/range/conflict errors deterministically.

No host function pointers, HLE handlers, or guessed system-library names belong
in that slice.

## Unknowns requiring stronger evidence

The following are **not** settled by this review:

- exact application entry-register/stack/process-parameter contract;
- exact bit packing of every module/library id and version field;
- rules selecting legacy versus current SCE dynamic-tag variants;
- module search order and dependency-resolution precedence;
- weak-symbol and duplicate-export precedence;
- library/module version compatibility policy beyond observed exact matching;
- lazy versus eager PLT binding behavior;
- exact RELRO transition timing;
- TLS module-index assignment and thread-pointer layout;
- startup/finalizer ordering across modules;
- whether specific behaviors vary by firmware generation;
- which container metadata affects underlying ELF mapping after authentication.

These should become AstraeaProbe questions rather than assumptions.

## Controlled tests implied by the evidence

Synthetic tests can be written without proprietary files:

- SCE file-type acceptance only when PS5 profile is enabled.
- Current and legacy dynamic-tag fixtures remain distinguishable.
- Conflicting singleton SCE records fail deterministically.
- Module and library IDs cannot alias accidentally in the model.
- Long-form symbol parsing preserves the raw string.
- Malformed NID/library/module spellings are rejected or retained as raw,
  according to an explicit contract.
- Unknown SCE dynamic tags survive parsing unchanged.
- Known relocation types are classified without applying unsupported semantics.
- Payload-style ELF metadata is not accepted as evidence of installed-title ABI.

Later controlled hardware probes should target the unknown list above and
record only observable results, never proprietary module contents.

## Current conclusion

There is enough convergent public evidence to justify the **shape** of an
Astraea PS5/SCE metadata layer.

There is **not** enough evidence yet to implement a broad PS5 dynamic linker or
to bind system calls by guessed names/NIDs.

The safest next sequence is:

1. finish this evidence map (#8);
2. specify AstraeaProbe v0 (#10);
3. define trace schema v0 (#6);
4. use controlled synthetic/hardware observations to settle unresolved ABI
   questions;
5. only then implement the first evidence-backed PS5 import-resolution slice.

