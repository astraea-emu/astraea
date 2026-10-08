# Independent native-title closure baseline — 2026-10-08

**Evidence status:** host-built public clean-room title, read-only Astraea analysis and supervised preflight only. **Not a retail title, firmware reference, PS5 console observation, native guest execution, or gameplay claim.**

## Exact source identities

- Astraea: PR #355; merge SHA to be attached only after six exact-head checks pass.
- Independent public GPL-3.0-or-later source: [blackbearreloaded/ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate/tree/2f672d1c2f508e26f82ce6e27cef289a0861413c), pinned at `2f672d1c2f508e26f82ce6e27cef289a0861413c`.
- Astraea observer integration: `tools/reference/ps5_process_entry_observer/prepare_blackbear_checkout.py`; same checkout modified offline and built with `make app` using the public SDK pinned by the external project.
- Input to Astraea diagnostics: **the host-generated** `build/eboot.elf`, never a Sony binary or downloaded retail title.
- Reproduction: `.github/workflows/c1-observer-build.yml`, "Probe public independent PS5-format title with Astraea." Exact-head successful integration attempt: [run 37834068382](https://github.com/astraea-emu/astraea/actions/runs/37834068382) (retry after an integrity-enforced transient zlib download mismatch). Further exact-head CI in PR #355 freezes the reported fields.

## Observed results (host-side, public input)

`astraea profile build/eboot.elf` exited successfully with:

| Field | Value |
| --- | ---: |
| program headers | 14 |
| PT_LOAD segments | 5 |
| mapped PT_LOAD memory bytes | 18,988 |
| executable PT_LOAD segments | 1 |
| executable mapped bytes | 8,564 |
| generic DT_NEEDED | 4 |
| SCE needed modules | 4 |
| SCE import libraries | 4 |
| unknown SCE dynamic records | 0 |
| dynamic symbols | 26 |
| RELA relocations | 40 |
| other relocations | 0 |
| TLS header present | yes |
| TLS initialized / total bytes | 0 / 0 |

`astraea diagnose build/eboot.elf` exited successfully, with the **first typed boundary**:

```text
boundary=unsupported_dynamic_dependencies
stage=pre_entry
guest_rip=0x10
detail0=12
detail1=0
```

This **does not** mean an emulated title entered CPU instruction 1. It says a public native-title artifact passes Astraea's structural reader sufficiently to reach a real module/dependency preflight stop.

## What this changes

Before this exercise, #334 appeared to be the only immediate C1 dependency. For this independently compiled sample the **earlier actionable software boundary is runtime dependency resolution**, not the initial-process ABI. Thus worthwhile PC-only work exists even if hardware observations are unavailable:

1. Precisely inventory the public sample's required modules, libraries, symbol/NID identities, weak imports, relocations and their source/toolchain provenance. Avoid logging binary content or inferring service semantics from symbol names.
2. Determine which dependent symbols are used **before the first bounded controlled stop**, rather than implementing all imported services.
3. Design a typed module graph and resolution policy **separate from the ELF parser**, avoiding fake-success unresolved imports. This is a future compatibility seam, not permission to bypass C1 hardware gates.
4. Use independent synthetic ELF fixtures and the open-source-built sample to ratchet structural and relocation behavior.
5. Do not implement host runtime values or service returns without an evidenced, testable contract.
6. Keep the full C1 direct-title ABI and normal PS5 startup observation experiment #334 open.

## Useful public comparative reference, not copied implementation

As of 2026-10-08, prosper's [#4744](https://github.com/mattias800/prosper/pull/4744) and [#4747](https://github.com/mattias800/prosper/pull/4747) document independently **console-measured** libc/RTC and kernel/user-service API results (104 and 64 calls respectively). [#4759](https://github.com/mattias800/prosper/pull/4759) proposes 77 AGC GetSize measurements; at this review it is **open**, not a merged contract. This is valuable for designing later HLE falsification experiments, especially avoiding return-zero defaults that are actually unsupported. The upstream repository explicitly states that it currently **grants no license** by default: do not vendor, copy or mechanically translate its code or tables into Astraea. Treat each published observation as bounded comparative evidence with its exact commit/firmware/test context, and corroborate before guest-visible promotion.

Related current scene risk classes from [KytyPS5](https://github.com/KytyPS5/KytyPS5) and [SharpEmu](https://github.com/sharpemu/sharpemu) include host-only x86 opcode gaps, GPU/CPU alias coherence, Wave64/NGG, Vulkan memory visibility, and host-driver differences. They are *prospective* post-entry bottlenecks, not evidence to create those subsystems ahead of a measured workload.

## Next gate

Freeze a **portable, explicit, fail-closed** public-sample module dependency classification from the read-only profile. Begin generic dependency-resolution work only when its callers, mapping lifetimes and refusal semantics are independently testable. Do not bind it to guessed Sony module behavior or title IDs. After software closure, #334 still supplies the normal-title startup observations necessary for promotion to real retail entry.
