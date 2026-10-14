# C1 — Pinned source-built PS5 ELF guest-data write and terminal fault

**State:** Research-only candidate; success requires the exact-head CI verifier, not simply a linked artifact. This is Astraea-authored code in a PS5/SCE-format executable produced by the reviewed public converter, **not independent Breakout, Sony SDK code, retail boot, or a validated PS5 startup ABI**.

## Source and artifact identity

The existing public-toolchain worktree at `blackbearreloaded/ps5-native-app-boilerplate@2f672d1c2f508e26f82ce6e27cef289a0861413c` is prepared through a third, separately selected mode `mov42-store-ud2`. The other `ud2` and `mov42-ud2` fixture modes remain unchanged.

Astraea's C++-authored entry contains exactly:

```asm
mov eax, 42
mov qword ptr [rip + displacement], rax
ud2
```

The symbol `astraea_minimal_marker` is a distinct source-defined, initialized, writable `unsigned long` with initial value `0x4153545241454101`. Its address is resolved by the **real ELF link**, not patched into code later.

The **first exact pinned-toolchain CI source build** reported:

| Property | Evidence |
| --- | --- |
| ELF SHA-256 | `3a69a4a035a10656c1532b1b8825eb4e361d2f755ac356ddeb7a36932f5c8cd7` |
| Length | 66,600 bytes |
| Entry guest VA | `0x10` |
| Original compiled instruction bytes | `b82a000000488905c4c000000f0b` |
| Actual writable marker guest VA | `0xc0e0` |
| Original marker bytes | `0141454152545341` |
| External system imports | None |
| RELA `R_X86_64_RELATIVE` | Eight |

These identities are **frozen constants** in both the CI source check and the mediated research verifier; a modified executable is refused before guest execution.

## Guest effect and fault acceptance

The mediated verifier loads only the exact pinned ELF, with original bounded PT_LOAD source contents and executable byte extents. It checks the exact RIP-relative displacement against the source-backed, writable data symbol. It applies/readbacks the eight source `RELATIVE` relocation values from the production Astraea read-only dependency report, then executes with a separate, synthetic stack.

Two fresh, independently allocated Unicorn 2.1.4 virtual-machine instances must each:

1. Retire the original `MOV EAX,42` and the original 8-byte RIP-relative store.
2. Make precisely one guest write to `0xc0e0`, changing its original value to `42`.
3. Receive a real `UC_ERR_INSN_INVALID` from the original `UD2` at RIP `0x1c`, with RAX still 42 and stack/flags unchanged.
4. Reject other guest loads, additional stores/instructions and permission-incompatible source accesses.
5. Compare **every mapped guest page** with an expected image reconstructed from the original source + reviewed relocations + one authorized store; source code, stack and relocation words must remain unchanged.
6. Reproduce identical final guest CPU state, memory-store summary and terminal exception context in both fresh runs.

Negative verification includes altered ELF source bytes, a changed relocation type and seven source-owned write-authority cases covering executable ranges, altered width or value, neighbouring addresses, empty width and address overflow.

## Security and architectural limitations

Like merged #470, this exact linked PS5 ELF contains different source virtual permissions within one 4-KiB host page. Unicorn maps **union backing-page permissions only in the disposable research engine**. The code hook authenticates the exact original two valid instructions and terminal `UD2` against their source executable byte extent; guest data hooks separately authorize the single known write only within original writable PT_LOAD bytes.

**This does not create a general sub-page permission broker.** Unicorn's generic memory callbacks are observations before successful write retirement, not independently trustworthy post-commit provenance or complete attack-surface mediation. Source-owned exact bytes plus post-execution **full memory comparison** establish the restricted case, not arbitrary outside-authored code safety. Native hostile-code admission (#410), retail C1 ABI (#334), genuine Breakout service `sceSystemServiceHideSplashScreen` (#378), GPU title frames and production engine selection remain blocked.

The goal of this increment is to turn a genuine source-built guest-memory effect into a measurable, typed positive/negative regression on the existing research infrastructure. No PS5 console operation or external binary redistribution is required.
