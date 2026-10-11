# Project Status

**Verified main: `e9b3201c` (2026-10-10).** C0 Linux read-only/pre-entry diagnostics and bounded V0–V3 graphics are merged. C1 remains active and **no commercial PS5 title has executed its first instruction, booted, produced a title-generated frame, reached a menu or become playable in Astraea**. This is an engineering-research emulator, not yet a playable PS5 emulator.

The [project plan](PROJECT_PLAN.md) defines durable C/V/S gates. This file is the **live merged frontier and next dependency**; open issues/PRs are proposals, not implemented functionality. [Research notes](research/README.md) and [ADRs](adr/README.md) preserve historical evidence.

## Milestone frontier

| Axis | Verified state |
| --- | --- |
| M0–M2 | Foundation, checked guest image and controlled **Astraea-owned** x86-64 execution complete |
| M3 / S0 | Typed failures, provenance, protected CI, sanitizers, fuzz smoke, trace/probes established |
| V0–V3 | Bounded owned AGC/RDNA2 → shader IR/SPIR-V → Vulkan readback proofs complete; **not** general PS5 game rendering |
| C0 | Linux x86-64 supervised, sealed-artifact **diagnostic/pre-entry** path complete |
| C1 | **Not complete:** evidenced real title-entry state and first genuine commercial instruction remain open |
| C2.0–C6 / S1–S5 | No commercial post-entry divergence, boot, game frame, menu, gameplay, broad compatibility or release readiness |

## Merged evidence (not game compatibility)

- **Loader / diagnostics:** bounded PS5/SCE ELF parsing, mappings, dynamic symbol/relocation/TLS inventories, source-pinned public artifact profiles, checked diagnostic classifications; production `diagnose` still rejects the independently authored raw Breakout ELF before entry.
- **Controlled native execution:** Linux/Windows owned probes, typed syscall and fault stops; Linux has a separate sealed-artifact worker and finite resource limits. A separately source-built **minimal PS5-format ELF** executed its deliberate first `UD2` instruction twice, after eight checked `R_X86_64_RELATIVE` writes, in an opt-in **synthetic entry** research worker ([#403](https://github.com/astraea-emu/astraea/pull/403)). This is neither Sony process-entry evidence nor a commercial game's first instruction.
- **Owned linking:** independently linked generic ELF client/provider `JUMP_SLOT` transfer, synthetic PS5/SCE-profile `GLOB_DAT` gate, checked patch/readback and negative cases. No full Sony module graph or arbitrary import binding is established.
- **Independent Breakout corpus:** SHA-256 `05c414993d1cd9e7182bbf5647be1ffad72c6ed6b6f5399d01af27281fef48b8` identifies the pinned PS5-marked raw ELF. Static profile: **5** needed library names, **20** dynamic symbols, **1** `RELATIVE`, **19** `GLOB_DAT`; source-to-ELF NID comparison matches **19/19** ([#404](https://github.com/astraea-emu/astraea/pull/404), [#406](https://github.com/astraea-emu/astraea/pull/406), [#408](https://github.com/astraea-emu/astraea/pull/408), [#409](https://github.com/astraea-emu/astraea/pull/409)). These counts **do not** establish resolved providers or executed service calls.
- **Breakout CPU-only reference:** [#413](https://github.com/astraea-emu/astraea/pull/413) merged a source-hash-guarded **host-compiled**, 480×270 first-frame reference (FNV-1a64 `11521d6d6940fe57`). The frame was **not** produced by Astraea guest execution or PS5 VideoOut.
- **Graphics:** selected owned shader/PM4/DCB and Vulkan raster/readback paths; no general PS5 shader ISA, GPU memory coherency, synchronization, tiling/compression, VideoOut, or title-rendered frame.
- **Verification:** five standard Linux/Windows/macOS/sanitizer/fuzz CI jobs plus pinned evidence workflows; passing a test only proves its asserted and explicitly scoped behavior. All six checks passed on the above `main`.

The larger pinned public native-title + non-Sony PRX studies remain preserved in [research](research/README.md): their `_init_env`, module-identity, relocation and runtime-contract uncertainty does not justify pretending a service is implemented.

## Active C1 dependency — ONE selected executable path

**Target:** the exact independently authored **Breakout ELF**, admitted only to an explicitly reviewed **test-only** Linux process boundary, reaching **one real, typed first imported-service stop** with its observed guest RIP and import identity. It is an integration milestone, **not C1 commercial-title completion**.

1. **Security prerequisite — [#410](https://github.com/astraea-emu/astraea/issues/410):** guest-IP-filtered seccomp is not hostile-code containment. Merged [#411](https://github.com/astraea-emu/astraea/pull/411) selectively denies 14 host-resource syscalls; merged [#412](https://github.com/astraea-emu/astraea/pull/412) rejects incomplete controller syscall identity. Neither closes the escape/capability threat. [Draft #414](https://github.com/astraea-emu/astraea/pull/414) *demonstrates* a permitted host write after a guest-to-host branch; [stacked draft #415](https://github.com/astraea-emu/astraea/pull/415) is an isolated child-process **test prototype**, not production admission. Require a genuinely restricted leaf without effectful controller IPC, trusted broker capability checks, fail-closed adversarial tests and bounded teardown; address sanitizer coverage without expanding leaf privileges. **Do not merge vulnerability-positive evidence as a permanent security invariant.**
2. **Breakout executable integration — [#378](https://github.com/astraea-emu/astraea/issues/378):** preserve exact ELF/source hashes; verify all required mapping and relocation writes with owned memory lifetime; distinguish the bare-NID source declarations from independently verified runtime provider/module identity; refuse ambiguity before any unsafe transfer. Reuse existing loader, patch and gate primitives, not new standalone prototypes. Produce two reproducible first **observed** imported-service stops, with typed unsupported result, exact guest RIP/NID and negative/tamper tests. No guessed Sony return values or general HLE catalog.
3. **First lawful retail title — in parallel, read-only:** select a simple offline, low-closure primary candidate (and backups) from public evidence; if a legitimately obtained, authorized executable is locally available, run **C0 static preflight only**. Do not commit retail bytes, keys, firmware or modules. This can happen before C1 native execution is ready.
4. **Reference PS5 — [#334](https://github.com/astraea-emu/astraea/issues/334):** collect two same-binary *normal home-screen native-title* pre-CRT/procparam observations **only when** a firmware-13.00-specific launch/logging route, frozen observer, precautions and a selected-path irreducible ABI question are confirmed. Payload-loader entry is not equivalent. Until then, **no console operation, network connection, firmware update or login is requested**.
5. **Commercial C1 then C2.0:** only after actual selected-path ABI evidence is sufficient ([#348](https://github.com/astraea-emu/astraea/issues/348)), reproduce the first lawful commercial instruction and then its first typed post-entry divergence ([#350](https://github.com/astraea-emu/astraea/issues/350)). Boot, frame, menu and playability are **later separate milestones**.

### Work selection rule

Every new PR should close a **measured first blocker** on the chosen executable path, establish required security/accuracy evidence, or prevent a documented regression. No second synthetic `GLOB_DAT` proof, broad HLE catalog, speculative graphics breadth, or new status document as a substitute for integrated progress. Use exact-head CI and explicit *what this does not prove* language.

## Separate, nonblocking work

- Graphics real-console evidence: [#191](https://github.com/astraea-emu/astraea/issues/191), [#207](https://github.com/astraea-emu/astraea/issues/207), only when an observed dependency requires it.
- Host ISA conditional boundary: [#319](https://github.com/astraea-emu/astraea/issues/319). Windows native arbitrary-title policy remains off: [#320](https://github.com/astraea-emu/astraea/issues/320).
- Linux Landlock/resource-hardening evaluation: [#298](https://github.com/astraea-emu/astraea/issues/298); **primary guest-code containment is #410**, not #298.
- GitHub governance and stale-branch cleanup: [#168](https://github.com/astraea-emu/astraea/issues/168). Preserve unique unmerged work; administration must not displace C1.

## Nonclaims and evidence discipline

Astraea has **no demonstrated commercial PS5-game compatibility**, full Sony startup ABI, hostile-code sandbox, game-generated GPU frame or user-ready release. Competitor screenshots/compatibility counts are prioritization context, not platform truth. No percentage of total emulator completion can be inferred from completed synthetic gates or test counts.

For durable architecture and milestone definitions see [PROJECT_PLAN.md](PROJECT_PLAN.md), [ADRs](adr/README.md), [clean-room policy](CLEAN_ROOM.md) and [research index](research/README.md).
