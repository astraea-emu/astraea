# Dependency manifest v0 — read-only, opaque identity evidence

**Historical contract (stage A):** PR #358 and subsequent symbol, relocation and module-graph stages have merged. The stage-B requirements below record the original design sequence, not an outstanding next action. For the current implementation frontier, see [Project Status](../STATUS.md).

This companion to the structural `astraea profile` command prints exact
dependency records from a locally supplied, lawfully available PS5/SCE ELF:

```text
astraea dependencies /path/to/eboot.elf
```

## Evidence contract

The command uses the same checked artifact loader, deterministic nonoverlapping
analysis-stack policy, validated PS5/SCE `GuestImage` and initialized image
view as the structural preflight path. It does **not** execute guest code,
initialize a system library, implement an HLE, resolve symbols or claim that
a declared dependency is actually called.

- Generic `DT_NEEDED` names are bounds checked against the validated
  string table. Up to 256 bytes are accepted per name, including non-UTF-8
  bytes; the manifest prints their exact **hex bytes** rather than decoding
  or printing potentially hostile terminal control characters.
- Each generic name retains its original dynamic-entry index, table offset
  and whether it duplicates an earlier **byte-identical** name.
- SCE needed-module and import-library records preserve exact raw dynamic tag,
  raw value and source index. Their opaque 64-bit values are **not** decoded
  into assumed module names, versions or NIDs. Duplicate markers represent
  exact equality of the raw values within each separately named category,
  not proof of identical runtime module identity.
- Record counts and unique counts are separate. The sum of generic needed,
  SCE needed-module and SCE import-library **records** is not the number of
  distinct modules, exports, functions or successfully resolved imports.
- `resolution=not_attempted`, `execution=none`, and
  `guest_instructions=0` are explicit machine-readable nonclaims.

Both the dynamic metadata table and the generic needed-record list have a
limit of 4096 records. Generic names longer than 256 bytes, out-of-bounds
name references, unterminated names, malformed ELFs and missing artifacts
produce a nonzero return with `manifest_error=` and do **not** emit a
partial success manifest. The limits protect diagnostics and are **not**
claims about PS5 loader limits.

The result is deterministic for the exact same ELF bytes. There is no
network access, firmware dependency, console access, access-control bypass,
emulator execution or third-party game content in these tests.

## Validation and next steps

The path-filtered pinned C1 workflow compiles an independently built public
normal PS5-format native application, runs `dependencies` on its actual
`build/eboot.elf`, and checks record counts, exact lossless hexadecimal
encoding, raw SCE fields, uniqueness bounds, and rejection of a malformed
artifact. Standard cross-platform CTest covers empty and missing artifacts.

Stage B of #356 must extend this **without replacing opaque evidence**:
enumerate individual dynamic symbol records through existing
`parse_dynamic_symbol` / `materialize_sce_dynamic_symbol` and
`parse_sce_dynamic_symbol_name`, distinguish defined and undefined,
strong/weak, long-form identity and unclassified raw strings, and cover
duplicate/malformed symbol cases with independently authored fixtures.

Only after acceptance of these identities should a separately scoped
`ModuleGraph`/link-resolution contract determine which dependencies can
be satisfied with independently implemented interfaces. No Sony ABI values,
default-success stubs or executable entry assumptions are admitted by the
manifest.

Relevant issues: #300, #334, #348, #356. The real PS5 two-run #334
hardware evidence remains outstanding independently of this command.

## Stage C: read-only relocation references

The static manifest additionally inspects the already validated REL, RELA,
PLT-REL or PLT-RELA descriptors, in a fixed order (general REL, general RELA,
then PLT). Each record includes the exact table kind and source-relative
table index, guest relocation target, raw relocation encoding, numerical
relocation type, referenced dynamic-symbol index and optional signed addend.

The total number of relocation records is bounded at **4096**. All records
must parse under the existing checked parser and refer to a symbol index
within the validated dynamic-symbol descriptor; otherwise the command fails
without emitting partial success. The manifest reports the number of
distinct *referenced symbol indices*, not the number of distinct HLE calls.

Crucially, `relocation_application=not_attempted` declares that the command
does not evaluate relocation architecture-specific semantics, mutate guest
memory, bind module exports, start system modules or attempt guest execution.
An ELF can be structurally enumerable while still being unexecutable by
Astraea. The first public native-title sample has **40 RELA records**;
the reproducible integration test enumerates and checks each reference.
