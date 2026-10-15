# C1 Landlock filesystem mutation restrictions — 2026-10-15

**Evidence category:** source-controlled, non-native independent Breakout research; this is **not** production retail execution or a complete hostile-code sandbox.

## Why this gate matters

Merged #478 entered an unchanged, hash-pinned independently authored Breakout ELF under a bounded Linux mediated Unicorn child and stopped before the first imported Sony call. Its Linux seccomp filter explicitly denies selected syscall numbers, but the policy remains default-allow for unlisted calls and does not provide a second independent filesystem access-control boundary. A correct check of guest memory cannot reverse irreversible host filesystem effects.

The Linux **Landlock** LSM adds an independent layer for *filesystem mutations* that is not based on the instruction pointer or a syscall-number denylist. The kernel documents that a ruleset handles selected access rights, rejecting accesses for which no allow rule applies; by choosing only mutation rights, ordinary file reads remain possible.

Official kernel reference: https://docs.kernel.org/userspace-api/landlock.html ; manual: https://man7.org/linux/man-pages/man7/landlock.7.html .

## Frozen behavior of this research increment

- Linux x86-64 only; requires a working Landlock ABI **3 or newer**. Unsupported or disabled Landlock fails closed on this optional supervised research path, not silently downgraded.
- A Landlock ruleset with **no allow paths** handles WRITE_FILE, REMOVE_FILE/DIR, MAKE_* entry types, REFER and TRUNCATE. It does not handle READ_FILE, READ_DIR or EXECUTE. No network restrictions are asserted.
- An independently owned, initially intact canary is created in a temporary directory **by the supervisor**; its path is handed to the child as a restricted research-only environment value. The child authenticates canary content and installs Landlock **before** installing the previous seccomp policy. It then executes three real host filesystem attempts against only this scratch path: writable open, unlink, and mkdir. All three must fail with **EACCES** from Landlock. A read-only reopen must succeed.
- After the child subsequently executes the original Breakout prefix and reaches the unchanged pre-import stop, the parent checks canary bytes and absence of a newly created directory. It does this after **each** of two fresh runs.
- The existing seccomp probes still separately require denied open/socket/fork operations, the same typed original guest stop, ten source-backed stack writes, immutable first GOT word, and no imported Sony call executed.
- The pinned CI additionally tests that restricted mode *without an owned canary* fails closed. The old direct non-restricted Breakout parity route is preserved.

## Security classification and negative result limits

Landlock is cumulative but **does not retroactively revoke file descriptors opened before restriction**. It does not prevent all possible host operations, all filesystem metadata changes, or attacks on the interpreter/runtime. The subprocess still uses Python/Unicorn; hostile-worker stdout can be falsified. A child could compromise itself, exploit remaining kernel capabilities, or potentially evade process-group cleanup through new process/session arrangements. The existing seccomp policy remains partial and the native guest-to-host RIP escape threat in #410 is not closed.

This gate deliberately does not select a production CPU engine, link/vend Unicorn, broaden HLE, guess a Sony API success state, establish genuine process ABI, or enable commercial PS5 game instruction execution. Keep ADR 0013 Proposed and #410 / #461 / #475 open.

## Acceptance

1. Original source-pinned Breakout pre-import stop and two bounded supervised runs pass.
2. Landlock ABI verification and three independent kernel EACCES filesystem probes pass **before** seccomp is activated.
3. Controller verifies unchanged canary after each run; no unauthorized canary mutation.
4. Source/trace/GOT tamper negatives and missing-canary refusal pass.
5. Seven exact-head CI checks pass, followed by careful diff/source review.

Related: [#410](https://github.com/astraea-emu/astraea/issues/410), [#461](https://github.com/astraea-emu/astraea/issues/461), [#478](https://github.com/astraea-emu/astraea/pull/478).
