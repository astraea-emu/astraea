/*
 * Astraea PS5 process-entry observer v0.
 *
 * This source is project-owned and intentionally transport-agnostic. It
 * snapshots the x86-64 entry state before a CRT/compiler prologue mutates it,
 * then tail-jumps into the ordinary project-owned CRT entry.
 *
 * Before the completion magic is published, the executable instruction stream
 * consists only of MOV-family instructions. MOV does not modify RFLAGS.
 */

#ifndef ASTRAEA_PS5_ENTRY_SYMBOL
#define ASTRAEA_PS5_ENTRY_SYMBOL astraea_ps5_entry_observer_start
#endif

#ifndef ASTRAEA_PS5_ENTRY_TARGET_SYMBOL
#define ASTRAEA_PS5_ENTRY_TARGET_SYMBOL astraea_reference_crt_start
#endif

#ifndef ASTRAEA_PS5_ENTRY_CAPTURE_SYMBOL
#define ASTRAEA_PS5_ENTRY_CAPTURE_SYMBOL astraea_ps5_entry_capture_v0
#endif

.text
.p2align 4
.globl ASTRAEA_PS5_ENTRY_SYMBOL
.type ASTRAEA_PS5_ENTRY_SYMBOL, @function
.extern ASTRAEA_PS5_ENTRY_TARGET_SYMBOL
.hidden ASTRAEA_PS5_ENTRY_TARGET_SYMBOL

ASTRAEA_PS5_ENTRY_SYMBOL:
.Lastraea_snapshot_begin:
    movq %rax, ASTRAEA_PS5_ENTRY_CAPTURE_SYMBOL + 0x10(%rip)
    movq %rbx, ASTRAEA_PS5_ENTRY_CAPTURE_SYMBOL + 0x18(%rip)
    movq %rcx, ASTRAEA_PS5_ENTRY_CAPTURE_SYMBOL + 0x20(%rip)
    movq %rdx, ASTRAEA_PS5_ENTRY_CAPTURE_SYMBOL + 0x28(%rip)
    movq %rsi, ASTRAEA_PS5_ENTRY_CAPTURE_SYMBOL + 0x30(%rip)
    movq %rdi, ASTRAEA_PS5_ENTRY_CAPTURE_SYMBOL + 0x38(%rip)
    movq %rbp, ASTRAEA_PS5_ENTRY_CAPTURE_SYMBOL + 0x40(%rip)
    movq %rsp, ASTRAEA_PS5_ENTRY_CAPTURE_SYMBOL + 0x48(%rip)
    movq %r8,  ASTRAEA_PS5_ENTRY_CAPTURE_SYMBOL + 0x50(%rip)
    movq %r9,  ASTRAEA_PS5_ENTRY_CAPTURE_SYMBOL + 0x58(%rip)
    movq %r10, ASTRAEA_PS5_ENTRY_CAPTURE_SYMBOL + 0x60(%rip)
    movq %r11, ASTRAEA_PS5_ENTRY_CAPTURE_SYMBOL + 0x68(%rip)
    movq %r12, ASTRAEA_PS5_ENTRY_CAPTURE_SYMBOL + 0x70(%rip)
    movq %r13, ASTRAEA_PS5_ENTRY_CAPTURE_SYMBOL + 0x78(%rip)
    movq %r14, ASTRAEA_PS5_ENTRY_CAPTURE_SYMBOL + 0x80(%rip)
    movq %r15, ASTRAEA_PS5_ENTRY_CAPTURE_SYMBOL + 0x88(%rip)

    /* The validated CRT already dereferences this exact startup prefix. */
    movq (%rdi), %r10
    movq %r10, ASTRAEA_PS5_ENTRY_CAPTURE_SYMBOL + 0x90(%rip)
    movq 8(%rdi), %r10
    movq %r10, ASTRAEA_PS5_ENTRY_CAPTURE_SYMBOL + 0x98(%rip)

    movq $0, ASTRAEA_PS5_ENTRY_CAPTURE_SYMBOL + 0x08(%rip)

    /* ASCII bytes "ASTRPEV0" in little-endian order. Publish last. */
    movabsq $0x3056455052545341, %r10
    movq %r10, ASTRAEA_PS5_ENTRY_CAPTURE_SYMBOL(%rip)
.Lastraea_snapshot_complete:

    /* Restore the only scratch GPR before handing off to the CRT. */
    movq ASTRAEA_PS5_ENTRY_CAPTURE_SYMBOL + 0x60(%rip), %r10
    jmp ASTRAEA_PS5_ENTRY_TARGET_SYMBOL

.size ASTRAEA_PS5_ENTRY_SYMBOL, .-ASTRAEA_PS5_ENTRY_SYMBOL

.bss
.p2align 4
.globl ASTRAEA_PS5_ENTRY_CAPTURE_SYMBOL
.type ASTRAEA_PS5_ENTRY_CAPTURE_SYMBOL, @object
ASTRAEA_PS5_ENTRY_CAPTURE_SYMBOL:
    .zero 0xa0
.size ASTRAEA_PS5_ENTRY_CAPTURE_SYMBOL, 0xa0

.section .note.GNU-stack,"",@progbits
