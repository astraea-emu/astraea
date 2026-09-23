# AI Chat Handoff Protocol

## Why this exists

Astraea is expected to outlive any individual ChatGPT/Codex/Claude conversation. Conversation context is useful working memory but must never be the only place where an architectural decision, experiment result, blocker, or next step exists.

## Source-of-truth order

When starting a fresh chat:

1. trust merged repository state for completed behavior;
2. inspect live open GitHub pull requests/issues for in-flight work and its
   exact branch/head;
3. read `docs/STATUS.md` for the last merged frontier, blockers, and next
   intended dependency/action;
4. read relevant ADRs/specifications for durable contracts;
5. use the previous chat summary only as working context.

An open PR is evidence of in-flight work, not completed project behavior. If a
chat or STATUS conflicts with merged code, stop and resolve the discrepancy
explicitly.

README.md and docs/PROJECT_PLAN.md intentionally describe durable
architecture/gates and must not be used to infer the current in-flight branch.
Do not hard-code ephemeral PR branch names into merged STATUS. A STATUS change
inside a PR should describe the expected **post-merge** frontier; live GitHub
PR/issue state supplies the branch while work is in flight.

## When to roll to a new chat

The lead assistant should recommend a fresh chat when one or more of these becomes true:

- the thread has crossed multiple major milestones/subsystems
- substantial earlier context is repeatedly being reconstructed
- answers begin depending on summaries rather than current repo state
- the user notices material UI/thread slowdown
- the assistant is uncertain whether an earlier decision is still current
- a major milestone has just completed and the next phase is logically distinct
- a clean context would reduce risk of contaminating a new design decision with obsolete assumptions

There is no need to rotate merely because a chat has many messages if context remains coherent.

## Capability boundary

The assistant cannot measure the ChatGPT UI's latency or exact remaining context window and cannot independently create a new user chat. It can, however, flag that a handoff is advisable during an active conversation and prepare the repository state needed for a safe transition.

## Mandatory pre-handoff steps

Before recommending a new chat:

1. merge or clearly identify all active work
2. inspect live open GitHub PRs/issues and record their exact heads in the
   handoff when work remains unmerged
3. update `docs/STATUS.md` to the expected post-merge frontier/next dependency
4. record any new architectural decision in an ADR
5. record unresolved research questions
6. ensure no important conclusion exists only in the chat
7. produce a concise handoff prompt pointing the next chat to the repository

## New-chat bootstrap prompt

Use a short prompt similar to:

```
Continue Astraea development from the GitHub repository astraea-emu/astraea.
Read README.md, docs/PROJECT_PLAN.md, docs/STATUS.md, docs/CHAT_HANDOFF.md,
relevant ADRs, and all currently open Astraea PRs/issues before changing code.
Treat merged repository state as authoritative. Resume from the exact
"Next action" in docs/STATUS.md and preserve the clean-room/evidence-first rules.
```

The new chat should retrieve current GitHub state instead of trusting a pasted historical mega-summary.

## Ongoing status discipline

Update `docs/STATUS.md`:
- after every meaningful merge
- when the merged next dependency changes
- when a blocker changes
- after hardware/probe findings
- before a chat handoff
- when milestone/gate state changes

STATUS committed in a PR should describe the intended state **after that PR
merges**, not the PR's ephemeral head branch. Discover in-flight branch/issue
state from live GitHub. Do not duplicate either source into README.md or
docs/PROJECT_PLAN.md. The status file should stay concise enough to read in
under two minutes.
