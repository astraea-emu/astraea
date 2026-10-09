# C1 public title + raw PRX: measured lexical demand boundary (2026-10-09)

**Evidence class:** read-only comparison of two independently authored, legally
reproducible PS5-format ELF artifacts. **No guest execution, relocation
application, real Sony module, discovered provider authority, boot or gameplay.**

## Source and reproduction

- Pinned external source checkout:
  blackbearreloaded/ps5-native-app-boilerplate @ 2f672d1c2f508e26f82ce6e27cef289a0861413c
  (GPL-3.0-or-later, inspected upstream build inputs).
- Runner: existing Ubuntu 24.04 C1 pinned observer workflow, with the
  exact source build and production Astraea dependencies command.
- New analysis: tools/reference/ps5_process_entry_observer/public_pair_lexical_demand.py,
  applied to the two already verified, complete read-only dependency manifests.
- [Passing pinned workflow 37982894151](https://github.com/astraea-emu/astraea/actions/runs/37982894151)
  for PR #389: the independently built title, raw PRX, unchanged structural
  baselines, the analyzer's synthetic classification/refusal self-test and
  the pair comparison all passed. Standard five-platform CI is a separate
  required gate for merging #389.
- JSON report is generated under runner temporary storage; the workflow log
  retains the observed summary, input digests and no-execution status.
  The temporary report is **not** a shipped runtime compatibility database.

## Exact reproducibility observations

| Artifact/input | Measured bytes | SHA-256 |
| --- | ---: | --- |
| Source-built observer-equipped title build/eboot.elf | 72,604 | b73ed4ed1a3b555b3d6ce22ab0153552b012865e7b83dff49ddd2a77478c9adf |
| Source-built raw clean-room libc.prx | 1,335,962 | 8ee6e124993e1af26420cb455890fd002f5d6c7e78883c860ce45734e7d002bb |
| Astraea title dependency manifest | — | 96e7a5766294a86cb2aa36423f26529ef9cad3af9d3fdcddd76b3518a49e90a5 |
| Astraea raw PRX dependency manifest | — | fa954e7033404ce0dc652967d87f415ff3a81a630bc46c9e42afd219e4c52a49 |

The title hash is for this particular observer-equipped host build. It is not
a claim that all toolchains/hosts produce identical title bytes. The PRX digest
and byte length are independently checked against the pinned public source
record before the cross-artifact analysis proceeds.

## What the manifests actually establish

The title contains **26** dynamic symbol rows and **40** RELA records.
Exactly **25 relocation references** request **25 distinct undefined long-form
SCE symbol identities** in this particular authored title. The raw clean-room
PRX contains **2,669** dynamic symbol rows, **2,566 defined global/weak
long-form export rows** and **1,896** relocation records.

For each of the title's 25 referenced identities, the analyzer checks the
raw NID/library/module text tuple against that PRX's defined long-form rows.
It classifies lexical text similarity, not a resolved module or callable ABI.

| Read-only classification | Referenced title relocations |
| --- | ---: |
| Full literal NID + library-ID + module-ID tuple appears among definitions | **0** |
| NID text matches, but full tuple does not | **8** |
| No equal NID text among the defined exports | **17** |
| **Total** | **25** |

**Consequences:** no exact full-triplet binding is evidenced by this pair.
The eight NID-only candidates are possible future investigations, not permission
to discard module/library qualifiers, choose a Sony provider, or return invented
values. IDs may be locally encoded *independently in each module*. The
remaining 17 references have no matching NID in this raw PRX; therefore one
non-Sony PRX cannot be presented as satisfying the title's external services.

Even if a full tuple matched, lexical equality alone would not establish
which provider the real loader selects, imported symbol version, calling
convention, strong/weak behavior, runtime initialization or correctness of
the actual function implementation.

## Fail-closed boundary and next decision

1. Preserve original source revision, both binary hashes, complete manifest
   hashes and the class counts. Any reproducibility drift or malformed report
   must stop the CI comparison; do not silently promote a changed corpus.
2. Keep generic ModuleGraph keys and source-owned generation/mapping epochs
   separate from raw SCE local IDs. The exact cross-binary ID association and
   defining-module authority remain **unverified**.
3. Do not write a blanket HLE implementation for the 17 missing NIDs or call
   the eight shared-NID rows resolved. Prioritize static classification of
   the **first actually required function and dependency** under selected
   title initialization, with independently checkable source evidence.
4. For any next real title, use **lawfully held** inputs and store only
   hashes, bounded structural/demand reports and typed stop evidence; do not
   vendor proprietary game/firmware bytes or infer compatibility from parses.
5. #378 remains open until the entire independently authored module lifecycle
   and negative criteria are satisfied. Commercial PS5 C1, normal-entry
   state and #334's genuine normal-title hardware observations are still
   unfulfilled. The PS5 should remain untouched for this host-only work.

The analyzer deliberately returns resolution=not_attempted,
guest_instructions=0 and never patches memory, launches guest instructions,
or calls an export.
