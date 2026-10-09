# Second independently authored PS5-format corpus: raw clean-room PRX

**Status:** first host-only diagnostic reproduction succeeded on the pinned independent PRX; exact regression assertions now added and awaiting their own full CI. Not PS5 runtime or gameplay evidence.

## Motivation and source

The first pinned public native PS5-format application from
`blackbearreloaded/ps5-native-app-boilerplate@2f672d1c2f508e26f82ce6e27cef289a0861413c`
contains 40 RELA relocation records, 25 externally referenced import
symbol indices and 15 null-symbol references. It does not stress large
dynamic-module metadata, export tables, or TLS-related relocation classes.

The **same reviewed GPL-3.0-or-later source checkout** also contains a
source-generated clean-room runtime module, `runtime/libc.prx`.
Unlike a Sony firmware library, it is generated from independent C++
sources and public text manifests. Its source is
`tooling/native/libc_builder.cpp`, driven by
`tooling/native/runtime/api-surface.txt` and `imports.txt`.

The pinned `docs/RUNTIME_SHIM.md` records:

- raw source-generated ELF: 1,335,962 bytes, SHA-256
  `8ee6e124993e1af26420cb455890fd002f5d6c7e78883c860ce45734e7d002bb`;
- 2,566 export records and 102 named system imports;
- 1,790 `R_X86_64_RELATIVE` relocations, 100 `JUMP_SLOT`,
  3 `GLOB_DAT` and 3 TLS module (`DTPMOD64`) relocations.

These are the **upstream project's statements about its authored
binary**, not Astraea verification of its semantics. The project says
some API exports are compatibility stubs; the raw ELF is NOT Sony libc.

## Experiment

The path-filtered pinned-native-title CI job, after compiling both the
public native title and the Astraea diagnostic CLI, invokes the pinned
`build/host/libc-builder` on its own text manifests and creates a raw
ELF in the runner's temporary directory. It verifies the resulting
artifact against the exact source-recorded byte length and SHA-256.

It then runs **only** `astraea profile` and the default,
opaque `astraea dependencies` read-only CLI on that raw ELF.
Both host diagnostics **succeeded** on the initial recorded pinned
artifact, so subsequent runs now require exact source-recorded structural
fields. The acceptance test does not permit an arbitrary typed refusal to
replace this established valid baseline. No guest instructions execute and
no import resolution is claimed.

The experiment does **not** execute either the PRX's startup code or
Astraea's retail guest path; it does not import a proprietary Sony binary
or use a PS5 jailbreak, console, keys or firmware.

## Observed Astraea results (first successful run)

The pinned-host [integration run 37875721907](https://github.com/astraea-emu/astraea/actions/runs/37875721907) verified the independently rebuilt raw ELF's exact
SHA-256 and byte length before invoking Astraea's real production
`profile` and `dependencies` commands. **Both returned exit code 0.**

| Field | Read-only observation |
| --- | ---: |
| Program headers | 14 |
| PT_LOAD segments | 5 |
| Load memory bytes | 1,294,514 |
| Generic needed modules | 3 |
| SCE needed-module entries | 3 |
| SCE import-library entries | 3 |
| Unknown SCE dynamic records | 0 |
| Dynamic symbols | 2,669 |
| General RELA relocations | 1,796 |
| PLT relocations | 100 |
| Total relocations | 1,896 |
| TLS present | Yes |
| TLS initialized / total bytes | 384 / 1,128 |
| TLS alignment | 16 |

The default dependency manifest also reported `execution=none`,
`resolution=not_attempted` and `guest_instructions=0`.
These are **Astraea-observed read-only properties** of this exact public
raw ELF, not confirmation of its runtime imports, symbol provider
behavior, relocation application or native entry.

The CI assertions were subsequently tightened to require these
values on the pinned exact source artifact. This higher-tier test
must pass on its new head before the PR is merged.

## Why it matters

This is a second, harder input corpus: a real source-built SCE dynamic
**module**, not merely the main native application. It can expose
missing parser invariants, unsupported relocation types, missing
symbol-count bounds, and malformed-asset handling that the smaller
40-relocation title cannot. Every resulting improvement must remain
generic, fail-closed, and tested on negative data.

Source manifest and recorded reference:
https://github.com/blackbearreloaded/ps5-native-app-boilerplate/blob/2f672d1c2f508e26f82ce6e27cef289a0861413c/docs/RUNTIME_SHIM.md

Related: #334 (still no hardware observations), #356 (dependency
closure), #367 (public metadata profile), #370 (first title demand).
