# AI Chat Handoff Protocol

## Why this exists

Astraea is expected to outlive any individual ChatGPT/Codex/Claude conversation. Conversation context is useful working memory but must never be the only place where an architectural decision, experiment result, blocker, or next step exists.

## Source-of-truth order

When starting a fresh chat, trust sources in this order:

1. merged repository state
2. `docs/STATUS.md`
3. ADRs/specifications
4. open GitHub issues and PRs
5. the previous chat summary

If a chat conflicts with merged repository documentation, stop and resolve the discrepancy explicitly.

`docs/STATUS.md` is the only document intended to carry the volatile active
branch, active issue, blocker, and exact next action. README.md and
docs/PROJECT_PLAN.md intentionally describe durable architecture/gates and
must not be used to infer a newer active frontier when STATUS/open GitHub state
says otherwise. If STATUS itself is stale immediately after a merge, reconcile
it in the next reviewed documentation change before beginning unrelated work.

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
2. update `docs/STATUS.md` with the exact active branch/issue and next action
3. record any new architectural decision in an ADR
4. record unresolved research questions
5. reconcile STATUS against currently open GitHub PRs/issues
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
- when the active branch/issue changes
- when a blocker changes
- after hardware/probe findings
- before a chat handoff
- when milestone/gate state changes

Do not duplicate the exact active branch/issue/next action into README.md or
docs/PROJECT_PLAN.md. The status file should stay concise enough to read in
under two minutes.
