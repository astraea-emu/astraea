# C1 bounded controller capture and descendant teardown — 2026-10-14

**Scope:** Source-owned Linux C1 research subprocess supervision. No commercial PS5 bytes, native outside-authored entry, production retail runtime, or Sony service call.

## Problem and prior negative boundary

Merged #476 used Python subprocess.run with stdout/stderr pipes and checked the stdout byte count only **after** the subprocess completed. The standard library communication API buffers full output in memory. The nominal 16-KiB report limit thus did not limit allocation in the trusted controller. A descendant inheriting open pipes could also prevent EOF after its original leader exited.

This is a controller availability gap separate from mediated CPU correctness. A child can also exit nonzero, send invalid JSON, or withhold EOF. Guest memory verification cannot repair those behaviors.

## Focused change

The research-only owned_bounded_worker_io.py captures stdout and stderr concurrently using Linux selectors and nonblocking pipe reads. Each read requests at most the remaining cap plus one byte; the first extra byte triggers refusal and process-group teardown. A single wall-clock deadline covers pipe progress and process exit. The parent closes streams and reaps the direct child on refusal.

start_new_session creates a child process group. Refusal sends SIGKILL to the whole group even when the original leader already exited. This closes the common same-group descendant pipe-hold case. It does **not** prevent a hostile child from creating an independent process group/session, exploiting the parent, or using existing host capabilities. PID namespace/cgroup/pidfd hardening is future work.

The existing #476 worker CPU/memory checks, two fresh runs, JSON schema, partial seccomp denial probes and typed UD2 terminal are unchanged. The 16-KiB stdout and 4-KiB stderr limits are now enforced **during reading**.

## Independent negative corpus

Ten source-authored child cases test: exact normal streams, concurrent high-volume pipes, exact-budget success, stdout overflow, stderr overflow, silent hang, nonzero exit, invalid capture budget, invalid JSON, and a leader-exit/descendant-pipe-hold with attempted delayed host file write. The last case must time out, kill the group, and show the marker file was never created.

These tests execute in the Linux C1 pinned workflow with the same seven-check exact-head merge policy.

## Trust limits

- This prevents one memory-exhaustion route and one descendant-lifetime failure, not all denial of service or privilege escalation.
- preexec_fn remains necessary for existing resource limits and is used only from the current single-threaded research parent. Python warns against preexec_fn in a multithreaded parent.
- Worker-reported CPU traces, denial strings and private pages are still supplied by the worker; this is not independent hostile-worker attestation.
- The #476 seccomp policy still permits unlisted host syscalls and does not harden Python/Unicorn memory corruption.
- No PS5 ABI, local game dependency profile, commercial first instruction, game frame, final CPU backend or playable title is established.

## Decision

Classify this as controller protocol hardening, NOT a host sandbox. Next prove stronger worker capabilities and message-trust boundaries rather than increasing synthetic instruction counts. Leave #410 and #461 open.

References: https://docs.python.org/3/library/subprocess.html ; https://github.com/astraea-emu/astraea/issues/410 ; https://github.com/astraea-emu/astraea/issues/461 ; https://github.com/astraea-emu/astraea/pull/476 .
