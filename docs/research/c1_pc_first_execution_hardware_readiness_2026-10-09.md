# Astraea C1: PC-first execution and reference-PS5 readiness (2026-10-09)

**Decision status:** active priority and evidence policy. The governing C/V/S milestones remain in `docs/PROJECT_PLAN.md`; current merged code and `docs/STATUS.md` take precedence. This document is the action map, not a promise of game support or a PS5 hacking tutorial.

## 1. Objective and nonclaims

The objective is an actual, repeatable PS5 commercial-title **first guest instruction** followed by the **first deterministic post-entry divergence** (C1 → C2.0), then C3–C6 gameplay. Work on PS5 hardware is deferred until the earliest selected-path uncertainty cannot be resolved using independently generated inputs and existing public evidence.

Astraea has **not** demonstrated commercial-title execution, normal PS5 entry-state ABI, real-title GPU frames, boot, menu, gameplay or superiority to other emulators. Do not equate a parser accepting an ELF, a synthetic guest probe, compiled SPIR-V or a relocation encoder with those milestones.

## 2. Verified reference frontier

- **Graphics V0–V3:** bounded Astraea-owned AGC/RDNA2 → SPIR-V/Vulkan, PM4/DCB and raster/readback experiments are merged. Real-title GPU coverage is unproven.
- **C0:** Linux x86-64 supervised `astraea diagnose <artifact>` with isolated worker, sealed artifact transfer, resource ceilings, syscalls and typed failures is merged. In an otherwise admitted image the deliberate entry stop is `unsupported_initial_process_abi`; the separately built public title first reports `unsupported_dynamic_dependencies`.
- **Small public native-title corpus:** pinned independently generated `blackbearreloaded/ps5-native-app-boilerplate@2f672d1c2f508e26f82ce6e27cef289a0861413c`. Its four generic needed filenames are `libSceLibcInternal.prx`, `libSceSystemService.prx`, `libSceVideoOut.prx`, `libkernel.prx`; 26 symbol rows (25 named SCE long forms), 40 RELA relocation records and a zero-sized TLS template/header. These are structural facts, not working services.
- **Second non-Sony PRX corpus:** same pinned public source checkout, independently generated raw runtime `libc.prx`, exact hash guarded by CI. Astraea profiles 2,669 symbol rows, 1,896 relocation records (1,790 RELATIVE, 100 JUMP_SLOT, 3 GLOB_DAT, 3 DTPMOD64), 103 distinct referenced symbol indices, 384 initialized / 1,128 total TLS bytes. Not a real Sony system library.
- **Owned code/linker primitives:** fail-closed symbol/needed-module inventories, opt-in public SCE packed metadata hypotheses, exact user-declared module graph, relocation/symbol binding plans, standard AMD64 owned import patch byte construction, checked single/batch guest-memory writes, and owned RELATIVE/DTPMOD64 encoders are separately tested. They **do not** establish the real PS5 provider graph, runtime TLS module-ID allocation, load ordering, or service semantics.
- **C1 reference observer:** pre-CRT assembly capture, pinned native-title integration, offline procparam normalizer and same-binary repeat comparator are built and host-tested. **Two real normal-title PS5 captures have not been collected**. #334 remains open and is the physical truth gate.

## 3. Implementation priority: retire the earliest selected-path unknown

| Order | Task | Exact stop/proof | Requires physical PS5? |
| --- | --- | --- | --- |
| P0 | Ensure exact-head CI and one accurate merged frontier | Five standard jobs, additional pinned corpus job when applicable; docs and issue states agree | No |
| P1 | Exercise real existing loader graph under lawful independent title + PRX profiles | Deterministic typed dependency closure report, bounded provenance, no fake success | No |
| P1 | Make generic source-owned module lifecycle demonstrable | Two authored modules: map ownership, dependency resolution, relocation plan/apply order, already mapped writable ranges, conflict/duplicate resolution, TLS module-ID provenance, unload/refusal; multi-module tests | No |
| P1 | Validate process start under synthetic **research-only** entry profiles | Independently authored guest executes inside supervisor to a repeatable stop; never promoted as Sony ABI | No |
| P2 | Choose first lawful real game workload for static preflight | Exact input hash, structural closure and relocation histogram, first unresolved categories; one main + two fallback titles if available | No, if lawful artifact already available |
| P2 | Comparative public emulator research | Specific source/spec comparison for *first observed* missing feature; test hypotheses independently with license review | No |
| H | Reference-console `#334` normal-title observation, only when readiness gates below pass | Two same-binary authentic observations, per-run JSON, normalized comparison, full provenance | **Yes** |
| P3 | Hardware-informed selected ABI `#348` and C2.0 `#350` | No unknown required entry fields; first genuine commercial guest instruction and repeatable post-entry typed failure | Primarily PC after H |
| P4 | Runtime/HLE/graphics selected-title closure | C3 sustained boot → C4 title-generated frame → C5 interactive menu → C6 defined playable route | Primarily PC |

**Do not treat a list of planned patches as proof of a working module loader.** An executable transition with isolated owned modules, negative cases and readback is the next useful holistic proof, followed by lawful selected-title evidence. Source-independent synthetic experiments can lower risk but do not establish PS5 behavior.

## 4. Hardware readiness criteria (ALL required before risky console work)

1. **Locked purpose:** name the irreducible question (normal native-title entry RDI/RSI/RBP/RSP, 16-byte process prefix and `sceKernelGetProcParam` relationship). Do not run an exploit to gather unspecified telemetry.
2. **Frozen exact artifact:** observer source/host-built intermediate and final ELF identified by SHA-256, no rebuild between the two runs, tools/report schema tested in CI.
3. **Actual normal-title route:** evaluate maintained project source and evidence for a **home-screen-launched independent application**, with kernel-log capture. An arbitrary ELF payload-loader handoff is not equivalent to title entry.
4. **Firmware-specific feasibility, rechecked just before use:** independently verify the precise firmware family and chosen exploit/runtime/launcher component revisions and compatibility. Historically reported console system software is `26.02-13.00.00.40`, i.e. 13.00. It has reportedly remained unplugged, so **never update it merely to perform the probe**.
5. **Security/data precaution:** user acknowledges risk of freezes/panics, filesystem corruption, account/online-service restrictions and lost data; backups and a recovery/stop procedure exist. No unverified packages or automatic payload bundles.
6. **Capture path rehearsal:** analyzer can process synthetic and host-built records; log transport can capture one complete bounded record from a benign normal-title test before the observer experiment.
7. **Exit criteria set:** stop at kernel panics, repeated filesystem/mount faults, launcher inability to start a normal title, or log integrity failure. Record the blocker and continue PC research rather than escalating blindly.

If firmware 13.00 lacks a validated normal native-title route, the fallback is an independently authorized contributor on a supported research console—not speculative privilege escalation or updating the user's console.

## 5. Historical firmware-13.00 feasibility: separate capability layers

These are **candidate source pointers**, not a verified combined end-to-end chain on the user's exact console.

- [Relapse-Exploit](https://github.com/ntfargo/Relapse-Exploit) advertises PS5 7.00–13.60 payload-loader support and explicitly warns of hangs/kernel panics. **Exploit success is not home-screen normal-title success**.
- [kstuff-lite v1.11](https://github.com/EchoStretch/kstuff-lite/releases/tag/v1.11) advertises 13.xx support but the release lists FPKG support only through a narrower firmware range. Avoid inferring that an FPKG title installs/launches on 13.00 from a generic firmware range.
- [ShadowMountPlus](https://github.com/drakmor/shadowMountPlus) has firmware/environment-specific mounting features and warns about corruption and shutdown failures. Its installation/mount path is a separate risk gate.
- [ps5link-sdk](https://github.com/Rufidj/ps5link-sdk), and the pinned BlackBear native-title boilerplate, distinguish real native title execution from payload loaders. The pinned BlackBear author documented console testing on 6.02/12.70, **not verified 13.00**.
- [ps5debug-NG](https://github.com/Pharaoh2k/ps5debug-NG) and [klogsrv](https://github.com/ps5-payload-dev/klogsrv) are potential register/log transport references; they do not establish observation at the exact pre-CRT primary-title-thread boundary by themselves.

Recheck current upstream commits, tags, archived reports and exact launcher dependencies shortly before physical work. No hardware or permanent console modification is requested by this document.

## 6. User action and organization

**Now:** nothing on the PS5. Keep it unplugged/unchanged, preserve firmware and do not sign into PSN solely for these experiments. PC-side GitHub CI/research can continue.

**Optional non-invasive preparation later:** photograph the offline `Settings → System → System Software → Console Information` page (not required while the user is away or tired), identify whether the console has any data worth backing up, and confirm availability of display/controller and a computer on the same local network. Do not initialize, update, install, exploit, connect untrusted USB content or make a test account yet.

**At the deliberately scheduled hardware session:** H0 firmware/backups/network/risks → H1 benign research-environment feasibility → H2 verified home-screen-native-title smoke and logging → H3 identical frozen observer native title twice → H4 copy raw lines + SHA-256/provenance → H5 run offline parser/comparator on PC → H6 selectively promote only supported selected-path ABI and return to PC development.

Physical attempts can **fail or be deferred**, and the project should retain that result. The run ends upon two authentic repeatable observations or a documented blocker, not arbitrary repeated exploits.

## 7. Competitive catch-up and release differentiation

PS5 projects such as SharpEmu, KytyPS5, prosperity/prosper and others already report real-title progression and have mature loader/HLE/GPU work. Their source is a **reference for hypotheses and engineering failure classes**, not a substitute for independently verified startup ABI. Honor each project's actual file licenses before reuse.

Astraea's long-term competitive proposition is **selected-title compatibility first**, then measured reliability and frame-time performance, with `Astraea Verify` as a product hypothesis: exact emulator build, lawful title hash, deterministic first divergence, supported route, hardware/driver context and sanitized logs. Do not call it a distinct market advantage until real users and like-for-like benchmarks demonstrate it.

At each engineering review: ask **which first-divergence dependency a new PR removes**, demonstrate the before/after result on an executable workload, and retain prior workloads as regression gates. Reject unrelated infrastructure, unmeasured shader breadth and undocumented title-specific hacks.

## 8. Stop/review triggers

Revisit the strategy **only** when a measured title changes the first blocking dependency; public API/hardware evidence invalidates an earlier assumption; a CI/fuzz bug reveals a correctness hazard; the 13.00 normal-title environment becomes demonstrably unavailable; or a better lawful artifact allows a more direct C1 approach.

Do not repeatedly rewrite the architecture merely because no game has run yet. Conversely, **do not spend months generating synthetic relocation helper PRs** if no general module-lifecycle or selected-title experiment is advancing. Use the readiness checklist to expose and fix that stagnation.

## Evidence source map

- Governing: `docs/PROJECT_PLAN.md`, `docs/STATUS.md`, `docs/adr/`, [C1 reference observation #334](https://github.com/astraea-emu/astraea/issues/334), [runtime closure #356](https://github.com/astraea-emu/astraea/issues/356).
- First public native title: `docs/research/public_native_title_closure_2026-10-08.md`.
- Second public PRX: `docs/research/public_cleanroom_prx_second_corpus_2026-10-08.md`.
- Public SCE identity hypothesis: `docs/research/public_sce_packing_v1.md`.
- Hardware observer and transport-neutral log analyzer: `tools/reference/ps5_process_entry_observer/README.md`.
