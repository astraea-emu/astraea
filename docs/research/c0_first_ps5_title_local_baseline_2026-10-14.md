# C0: First commercial PS5 title — provisional selection and local baseline

**Decision checkpoint:** 2026-10-14. **Status:** shortlist selected from lawful public evidence; **NO actual commercial game executable supplied or analyzed**. This document does not assert first-instruction execution, title boot, game rendering or compatibility. The production retail path remains default-deny and pre-entry.

## The shortest first-title candidate

| Priority | Candidate | Evidence supporting PS5 eligibility | Unknown requiring measured C0 input |
| --- | --- | --- | --- |
| Provisional primary | **Dreaming Sarah**, Canadian PS5 product `PPSA02929` | Sony's Canadian store explicitly lists PS4 and PS5 versions. AnyPS5's official repository and SharpEmu community report reaching gameplay on specific builds/Windows hosts. Small 2D platformer is a candidate for lower graphics closure, **not a proven simpler import path**. | Exact legal artifact, SHA/revision, executable format, dynamic dependencies and module graph, first guest import/entry slice, PS5-native ELF authenticity |
| Reserve: custom-engine class | **Dead Cells**, Canadian PS5 product `PPSA15552` | Native PS5 release shown by PlayStation; prosper's public `COMPATIBILITY.md` reports Prisoners' Quarters gameplay on a specific title revision. | Likely different runtime/library needs; actual version and ELF must be independently profiled |
| Reserve: additional small 2D class | **Animal Well**, Canadian PS5 product `PPSA06799` | Native PS5 release is on Sony's Canada storefront; simple-looking 2D graphics do not establish runtime simplicity. | Community compatibility entries sometimes refer to **other regional PPSA identifiers**; do not equate those with the Canadian executable absent exact hashes |

Primary sources:
- [Sony Canada — Dreaming Sarah](https://store.playstation.com/en-ca/product/UP0891-PPSA02929_00-RAGDREAMINGSARAH)
- [AnyPS5 README / game claim](https://github.com/ludnix/anyps5)
- [SharpEmu — Dreaming Sarah report](https://sharpemu.dev/blog/dreaming-sarah-is-now-playable-on-sharpemu-ps5-emulation/)
- [Sony Canada — Dead Cells](https://store.playstation.com/en-ca/product/UP4016-PPSA15552_00-DEADCELLSPS5SIEA)
- [prosper's exact-title compatibility tracker](https://github.com/mattias800/prosper/blob/main/COMPATIBILITY.md)
- [Sony Canada — Animal Well](https://store.playstation.com/en-ca/product/UP7425-PPSA06799_00-9351926314516028)

These are **external project reports**, not Sony API/ABI truth and not measurements performed by Astraea. The primary selection is conditional on **actual** local static analysis. Do not select solely from another emulator's frames-per-second or screenshot.

## New reproducible C0-only measurement route

Run the already built Astraea binary on a **lawfully controlled local PS5-native ELF**, which must be an actual readable **ELF**, not an encrypted package or PS4 executable. No game bytes should be committed, shared, sent to CI, or uploaded. A PS5 purchase or storefront ownership is not by itself evidence that a supported local executable is accessible; this guide provides no bypass of access controls.

On Linux, from an Astraea checkout:

```bash
python3 tools/reference/first_title_c0_baseline.py \
  --astraea ./out/build/linux-dev/astraea \
  --artifact /local/authorized/game/eboot.elf \
  --title-id PPSA02929 \
  --format sce \
  --include-preentry > local-c0-baseline.json
```

On macOS/Windows or when pre-entry diagnostic support is unavailable, omit `--include-preentry` and use the locally built analyzer executable appropriate to that host. For independently marked PS5 OSABI9/2 **ET_DYN** raw ELF research inputs only, explicitly select `--format ps5-marked-raw`; that mode supports structural analysis, **not** production `diagnose`.

The local tool:
1. Computes the source file's SHA-256 and length without copying its bytes to another system.
2. Runs each of Astraea's existing read-only `profile` and `dependencies` commands **twice** and rejects nonidentical reports.
3. Cross-checks module counts, import-library counts, dynamic symbols when available, relocation arithmetic and TLS aggregate invariants.
4. Optionally runs the **pre-entry** Linux retail `diagnose` twice and rejects any output claiming a native guest stage. No new execution path is enabled.
5. Emits only a bounded JSON containing verified aggregate counters, source SHA/size, versioned schema, explicit `guest_executed_instructions=0` and a typed pre-entry boundary if requested. It intentionally discards decoded symbol names, original raw relocation rows, local paths and CLI error text.

There is currently **no genuine Dreaming Sarah C0 result**. On completion, compare the exact baseline to the similarly measured reserves; choose the smallest authentic dependency graph and reproducible first stop. If only PS4 artifacts or inaccessible/encrypted packages are available, record that constraint instead of substituting PS4 binaries or guessing a game profile.

## Decision gates before attempting a genuine first instruction

- **C0 availability:** lawful native-PS5 artifact, exact product/revision, reproducible `profile`/`dependencies` and optional `diagnose` report locally preserved. This requires no guest execution.
- **Execution security:** #410's untrusted control-flow escape is unresolved in the native path; #461's mediated CPU candidate is not adopted as a production engine. Do not turn research memory snapshots into host containment claims.
- **Process startup:** the selected title's PS5 entry ABI/stack/procparam/TLS/bootstrap requirements remain unknown (#334, #348). Only fields actually observed before the first selected stop should block entry.
- **Service closure:** first actual title-specific import must be measured, not guessed. The source-pinned homebrew Breakout still stops at `libSceSystemService::sceSystemServiceHideSplashScreen` at `RIP=0x23c` (#378).
- **Actual game milestones:** C1 first genuine game instruction -> C2 first typed runtime divergence -> C3 sustained initialization/boot -> C4 game-generated frame -> C5 menu -> C6 playable route. V0-V3 source-owned graphics evidence does not satisfy C4.

**Operational rule:** do not ask for a PS5 firmware update, network connection, launcher, exploit or console action merely to run a local C0 baseline. A hardware observation is a separately authorized later gate once an actual minimal requirement and safe launch route have been demonstrated.
