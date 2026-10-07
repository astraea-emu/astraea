# PS5 emulation scene re-entry review — 2026-10-07

## Purpose

Re-evaluate Astraea after the repository pause following the 2026-09-25 C0
merge. This is comparative engineering research, not a source of
Sony-specific behavioral truth.

## Astraea checkpoint

Authoritative merged main before this review:

- `84b0cda44438c4ad54b45d3641483736cdcd8e84`;
- production Linux x86-64 `astraea diagnose <artifact>` is merged;
- C0 supervised retail diagnostic infrastructure is complete;
- otherwise-ready PS5/SCE images stop at `unsupported_initial_process_abi`;
- #300 is the active retail critical path;
- #191 remains a separate LinkShaders evidence/runtime-completeness track.

PR #301 already contains the post-C0 roadmap correction: bounded V0-V3
graphics and C0 retail diagnostics are complete, while C1 remains blocked.

## Comparator findings

### KytyPS5

Repository: `KytyPS5/KytyPS5`.

The project is moving rapidly across shader ISA, HLE, GPU resources, launch
UX and real-title integration. The 2026-10-07 commit labelled `agc: new abi`
was inspected directly: it adds
`AgcDcbSetShRegistersIndirectGetSize`, returning five dwords of command
storage. It expands AGC/HLE breadth but does not provide new LinkShaders
return-value evidence and does not change #191.

Kyty's loader synthesizes an argc/argv-like entry block, an emulator-owned
teardown callback and a guest stack before invoking a selected title entry.
This is useful independent implementation convergence for #300, but it is
not a hardware observation of the PS5 loader contract.

### Force67/prosperity

Repository: `Force67/prosperity`.

Recent work includes PS5 savedata, blank flips/vblank events, memory
metadata, shader subgroup handling and broader runtime integration.

The important C1 comparison is startup: Prosperity currently enters
libkernel first, with a FreeBSD-like initial stack and a non-zero PS5 TCB/FS
base, instead of directly entering the title. Its firmware-derived TCB
details are experiment clues, not Astraea constants.

This makes **bootstrap entry vs title entry** an explicit C1 distinction.

### SharpEmu

Repository: `sharpemu/sharpemu`.

Current work demonstrates advanced real-title GPU pressure around page
dirtiness, buffer/image visibility, device-address resources, render scaling
and shader translation. A 2026-10-07 change also reverted a large batch of
Silent Hill rendering/performance merges, reinforcing the value of exact-head
review and regression discipline under compatibility pressure.

### mattias800/prosper

Repository: `mattias800/prosper`.

This project was not included in the September comparison and is now one of
the most valuable public comparators. It is a user-space PS5-to-PC
compatibility layer with native x86-64 execution, clean-room HLE, AGC/Vulkan
translation and an RDNA2-to-SPIR-V recompiler. Its current roadmap reports
multiple titles at an automatic snapshot-guard rung and uses unusually strong
evidence, falsification and regression discipline.

Its proposed architecture sequence independently converges on several
boundaries Astraea already adopted: typed GPU command representation, a pure
SSA-like recompiler layer, page/resource ownership, typed guest pointers,
declarative HLE tables, conformance suites and replay/regression gates. This
is evidence against replacing Astraea's architecture merely to chase current
compatibility breadth.

For C1, current `prosper` code directly enters the title with a SysV-style
argc/argv/env/auxv stack, points RDI at that initial vector, uses RSI=0, and
activates a guest FS/TCB before entry. That is a third implementation choice:
it is useful comparative pressure, but it is still emulator behavior rather
than a controlled PS5 loader observation. In particular, working titles under
an implementation with RSI=0 must not be misread as evidence that the
ps5link-observed teardown role is false or optional on hardware.

Licensing is decisive: the repository README explicitly states that no
`LICENSE` file exists and that no license is granted by default. Astraea may
study public architecture/evidence and reproduce independently supported
behavior, but must copy **no implementation code** from this repository absent
an explicit license or permission.

### blackbearreloaded/ps5-native-app-boilerplate

Repository: `blackbearreloaded/ps5-native-app-boilerplate`.

This project materially changes the C1 evidence picture. Its clean-room
runtime documentation states that the startup behavior was independently
designed and implemented, and that the exact generated native title/runtime
artifact was hardware-validated on PS5 firmware 6.02 and 12.70.

Its project-owned `_start` consumes the same direct-title prefix as the
ps5link lineage: first argument/process block, argc-like value at +0, argv at
+8, and the original process pointer passed to `_init_env`.

That is enough independent hardware-exercised corroboration to promote the
prefix shape into typed Astraea C1A code. It does **not** prove a mandatory
non-null RSI teardown callback, initial RSP layout, FS/GS/TLS, or bootstrap
ordering.

The repository is GPL-3.0-or-later at the project-owned source level reviewed
here. License compatibility does not remove Astraea's provenance rule: prefer
small independently justified implementations over copying broad toolchain
code.

### ps5rs

Repository: `claimore22/ps5rs`.

This Rust project is useful for loader/HLE decomposition, deterministic fixture
generation, fuzzing and schema discipline. Its host-side emulator executes
guest x86-64 natively, but its current guest-entry trampoline primarily
switches to a prepared stack and calls the entry point; it does not provide
independent PS5 hardware process-entry evidence.

It is explicitly GPL-2.0-only, so its implementation code must not be copied
into Astraea's GPL-3.0-or-later tree.

### PortPS5 / AnyPS5 relinker lineage

`yuriolive/PortPS5` is a Windows-first native compatibility layer that relinks
PS5 ELF input into PE images and provides replacement system libraries under
the System V ABI. Its architecture rules—generic mechanisms, typed config, no
title-id branches in core, strong CI policy—are useful comparative process
evidence.

Its relinker changes the original launch boundary, so it is not C1 loader-entry
evidence. PortPS5 is GPL-2.0-only and must not be copied into Astraea without
relicensing/permission.

### ps5link-sdk / SharpProspero

Repository: `Rufidj/ps5link-sdk`; startup lineage derives from
SharpProspero.

The pinned real-title CRT evidence remains useful: RDI parameter block,
argc-like first field, argv at +8, RSI teardown, and `_init_env` receiving
the original parameter pointer. The October review found no second
independent controlled observation that turns this lineage into a complete
title-loader ABI.

## Architecture verdict

No foundational rewrite is justified.

Astraea should preserve:

- controller vs untrusted retail worker;
- loader structure vs runtime/module behavior;
- guest addresses/resources vs host pointers/Vulkan objects;
- raw PM4/AGC provenance vs typed Graphics IR;
- generic RDNA2 semantics vs PS5 AGC/stage ABI;
- semantic Shader IR vs compiler/value IR;
- evidence-backed PS5 behavior vs comparative emulator behavior.

The main refinement is C1: model **bootstrap-entry requirements** separately
from the later **title-entry contract**. A direct-title HLE strategy may
reproduce required bootstrap effects without executing proprietary system
modules, but those effects still need evidence.

## Reuse / fork / copy policy

Astraea is GPLv3-or-later. At the reviewed revisions:

- `Rufidj/ps5link-sdk` ships a GPLv3 license text;
- `KytyPS5/KytyPS5` ships a GPLv2 license text;
- `Force67/prosperity` ships a GPLv2 license text;
- no root `LICENSE` or `COPYING` file was available through the
  `sharpemu/sharpemu` repository API during this review;
- `blackbearreloaded/ps5-native-app-boilerplate` uses GPL-3.0-or-later for the
  reviewed project-owned source;
- `claimore22/ps5rs` is GPL-2.0-only;
- `yuriolive/PortPS5` is GPL-2.0-only;
- `mattias800/prosper` explicitly states that it currently grants no license
  by default because it has no `LICENSE` file.

A repository-level license file does not settle every file's `-only` /
`or later` status or third-party provenance. Therefore:

1. reference behavior and architecture with pinned provenance;
2. prefer clean-room reimplementation from primary public specifications or
   controlled observations for PS5-specific behavior;
3. do not paste Kyty, Prosperity or SharpEmu implementation code into
   Astraea without a per-file license/provenance audit;
4. even for license-compatible sources, preserve attribution and ask whether
   direct copying weakens Astraea's evidence provenance;
5. use forks as temporary research/differential sandboxes, not as a reason
   to replace Astraea's architecture.

## Current decision

The scene has validated likely post-C1 dependency classes—runtime linking,
TLS, process/thread services, page/resource tracking, richer shader compiler
passes, synchronization and presentation. It also produced enough independent
hardware-exercised evidence to promote the direct-title RDI/process-block
prefix, but not enough to enable native retail entry.

Keep `unsupported_initial_process_abi` in production. Continue #300. Encode
the corroborated partial C1A prefix now, then use transport-neutral observation
to resolve RSI teardown, RSP, TLS/TCB and bootstrap ordering before constructing
an executable process-entry profile.

## Conclusion

Astraea is behind leading projects in raw compatibility breadth, but its
architecture is not obsolete. The new scene should be used as a comparative
oracle and source of experiment questions, while Astraea retains stricter
evidence, provenance, containment and regression discipline.
