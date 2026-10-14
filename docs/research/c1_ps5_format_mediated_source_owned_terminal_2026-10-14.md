# C1 — Full source-built PS5-format ELF through mediated CPU to UD2

**Status:** bounded research candidate; acceptance determined by exact-head CI.
**Artifact:** separately source-built, Astraea-authored PS5-format ELF from pinned
`blackbearreloaded/ps5-native-app-boilerplate` toolchain commit
`2f672d1c2f508e26f82ce6e27cef289a0861413c`.
This is **not independent SharpEmu Breakout, a retail executable, or a Sony
process-entry ABI observation**.

## Purpose

The source-built two-instruction ELF had already passed native source-owned
Linux x86-64 ptrace verification (#447–#448), but did not have a matching
portable **mediated execution** result for the *same complete converted*
PS5-format ELF and its original eight `R_X86_64_RELATIVE` records.

Instead of extending Breakout past an unverified system-service boundary,
run this import-free real source-built ELF to a deterministic terminal fault
inside the *separate, pinned* Unicorn 2.1.4 research engine.

## Exact verified input contract

- Compiled PS5-marked ELF64, `ET_SCE_EXEC` `0xfe10`, x86-64, entry `0x10`,
  frozen size 66,600 bytes and SHA-256
  `cdeab7a242aa88a90a01494ff85ac68f44b15e7a2e588498927e3aa6aae5a947`.
- Original entry bytes `b8 2a 00 00 00 0f 0b` from a uniquely mapped
  executable PT_LOAD segment, with no modified or substituted code.
- Astraea's existing **production read-only** `dependencies` report
  must declare no external modules/import libraries, exactly eight
  uniquely writable and non-overlapping zero-symbol RELA `RELATIVE`
  relocations, and no unsupported named external symbols.
- Source ELF load regions and file backing are bounded and checked.
  The exact pinned converter produces **mixed-permission 4-KiB pages**:
  disjoint original virtual PT_LOAD byte extents share a host page even
  though their requested permissions differ. The first CI run correctly
  refused `mixed_guest_page_permission_requires_broker` rather than
  silently declaring them safe.
- For **this exact two-instruction source-owned workload only**, the
  mediated engine maps the union of required backing page permissions,
  records that a collision occurred, checks each executed instruction
  belongs to its **original executable PT_LOAD byte extent**, and stops
  on **any guest data read/write**. A fixed `MOV EAX,42; UD2` sequence
  requires neither data loads nor stores. This is a restricted experiment,
  **not guest byte-range permission mediation** for arbitrary machine code.

## Research execution

The script `execute_pinned_owned_ps5_elf_mediated.py` stages the exact
PT_LOAD bytes into Unicorn-owned **virtual memory** (never host-executable
guest mappings), applies/readbacks the eight zero-bias RELATIVE words from
the frozen diagnostic report, installs original virtual page permissions
and a distinct synthetic RW stack, then executes in **two separate fresh
engine instances**.

The source instruction `MOV EAX,42` must retire, followed by original
`UD2` raising a typed invalid-instruction error with RIP still at
entry+5 and RAX=42.

During qualification, Unicorn's pre-instruction code hook at the **invalid
UD2** address `0x15` reported width **`0xF1F1F1F1`**, not a valid decoded
two-byte length. An early guard correctly refused that untrusted width, but
also prevented fault delivery. The final guard authenticates the **exact
original `0F 0B` bytes** and a two-byte original executable virtual extent
at this one known faulting RIP, ignoring only the invalid decoder's reported
width. It still demands a subsequent actual typed invalid-instruction
exception, the same RIP/RAX, exactly the two expected code callbacks and
zero guest data reads/writes. This is no general invalid-opcode decoding
policy or host-isolation guarantee. No unreviewed code address or guest memory write is
accepted; source code and all relocated words remain unchanged. The two
runs must agree exactly on the observed terminal CPU/stack snapshot.

The entire original PS5 process initialization environment is not
represented: startup context and stack are explicitly synthetic,
there are no guest-created threads, no system modules, no HLE, no
VideoOut/AGC guest frame, and no externally authored guest admission.

## Negative controls / next decision

Refuse a changed ELF hash and a changed source relocation type before
guest entry. Do not turn `UD2` into a successful game-exit API, nor treat
the imported Breakout service as implemented. Subsequent work should
reuse a **bounded, source-owned guest ELF** with a meaningful observable
write/exit contract and grow a mediated mapping/fault API only where
needed, while separately qualifying `sceSystemServiceHideSplashScreen`
and the #410 native containment threat model.

A passing study does not select Unicorn for distribution: version-specific
GPL licensing, cross-platform embedding, fault/write mediation and
representative workloads still require decision evidence under ADR 0013.
