# Clean-Room and Provenance Policy

## Scope

Astraea is a compatibility research and emulation project. This repository must remain independently implementable, auditable, and free of unauthorized proprietary material.

## Never commit

- Sony firmware images, firmware components, encryption keys, secrets, certificates, or decrypted proprietary modules.
- Sony proprietary SDK headers, libraries, documentation, source code, or binaries obtained under terms that do not permit redistribution/use here.
- Retail game executables, decrypted game content, copyrighted assets, disc/package dumps, or other third-party content without explicit redistribution permission.
- Exploit payloads or instructions whose purpose is to bypass access controls rather than measure behavior needed for interoperability research.
- Code copied or mechanically translated from proprietary source.

## Permitted evidence classes

Each platform-specific claim should be attributable to one or more of:

1. **Public primary documentation** — AMD ISA manuals, Khronos specifications, public standards, patents, vendor-published open-source code, official platform documentation that is lawfully public.
2. **Public independent research** — papers, technical write-ups, talks, and open-source emulator/research projects used as comparative evidence.
3. **Controlled observation** — behavior measured using software/hardware the contributor is authorized to use.
4. **Synthetic inference** — a hypothesis derived from tests; it must be labelled as a hypothesis until corroborated.

## Provenance records

Non-trivial platform behavior should record:
- behavior/question
- evidence source(s)
- date accessed/observed
- confidence: low / medium / high
- known conflicting evidence
- tests that encode the behavior
- implementation locations affected

Prefer durable notes under `docs/research/` or a subsystem specification over comments that merely say "matches hardware".

## Clean-room implementation rule

When studying another emulator, separate **what it demonstrates** from **how it implements it**.

Acceptable:
- compare externally observable behavior
- learn names of concepts/APIs already public
- study architectural approaches and public interfaces
- run differential experiments
- read code whose license permits study and independently implement behavior with documented provenance

Before incorporating third-party source code, dependencies, generated tables, or substantial algorithms, document the license and compatibility implications in an ADR.

## Hardware research

Hardware experimentation must use devices/software the researcher is authorized to use. Astraea's core must not require circumvention tooling. Probe support is an optional research input, not a prerequisite for compiling or using the clean-room core.

## Contribution attestation

By contributing, a contributor represents that the submitted material is their own work or is incorporated under a compatible license with provenance documented.

If provenance is uncertain, do not merge the material until reviewed.
