.text
.globl astraea_test_invoke_entry_observer
.type astraea_test_invoke_entry_observer, @function
astraea_test_invoke_entry_observer:
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movabsq $0x1111111111111111, %rax
    movabsq $0x2222222222222222, %rbx
    movabsq $0x3333333333333333, %rcx
    movabsq $0x4444444444444444, %rdx
    movabsq $0x5555555555555555, %rsi
    /* RDI intentionally remains the process-prefix pointer from the caller. */
    movabsq $0x7777777777777777, %rbp
    movabsq $0x8888888888888888, %r8
    movabsq $0x9999999999999999, %r9
    movabsq $0xaaaaaaaaaaaaaaaa, %r10
    movabsq $0xbbbbbbbbbbbbbbbb, %r11
    movabsq $0xcccccccccccccccc, %r12
    movabsq $0xdddddddddddddddd, %r13
    movabsq $0xeeeeeeeeeeeeeeee, %r14
    movabsq $0xffffffffffffffff, %r15

    /*
     * Establish a known arithmetic-flags pattern immediately before entry.
     * CMP x,x yields ZF=1 and PF=1 while clearing CF/AF/SF/OF. CALL, MOV and
     * JMP preserve those arithmetic flags.
     */
    cmpq %rax, %rax
    call astraea_ps5_entry_observer_start

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx
    ret

.size astraea_test_invoke_entry_observer, .-astraea_test_invoke_entry_observer

.globl astraea_reference_crt_start
.type astraea_reference_crt_start, @function
astraea_reference_crt_start:
    movq %rax, astraea_test_target_capture + 0x10(%rip)
    movq %rbx, astraea_test_target_capture + 0x18(%rip)
    movq %rcx, astraea_test_target_capture + 0x20(%rip)
    movq %rdx, astraea_test_target_capture + 0x28(%rip)
    movq %rsi, astraea_test_target_capture + 0x30(%rip)
    movq %rdi, astraea_test_target_capture + 0x38(%rip)
    movq %rbp, astraea_test_target_capture + 0x40(%rip)
    movq %rsp, astraea_test_target_capture + 0x48(%rip)
    movq %r8,  astraea_test_target_capture + 0x50(%rip)
    movq %r9,  astraea_test_target_capture + 0x58(%rip)
    movq %r10, astraea_test_target_capture + 0x60(%rip)
    movq %r11, astraea_test_target_capture + 0x68(%rip)
    movq %r12, astraea_test_target_capture + 0x70(%rip)
    movq %r13, astraea_test_target_capture + 0x78(%rip)
    movq %r14, astraea_test_target_capture + 0x80(%rip)
    movq %r15, astraea_test_target_capture + 0x88(%rip)

    pushfq
    popq %r10
    movq %r10, astraea_test_target_rflags(%rip)
    ret

.size astraea_reference_crt_start, .-astraea_reference_crt_start

.bss
.p2align 4
.globl astraea_test_target_capture
.type astraea_test_target_capture, @object
astraea_test_target_capture:
    .zero 0xa0
.size astraea_test_target_capture, 0xa0

.globl astraea_test_target_rflags
.type astraea_test_target_rflags, @object
astraea_test_target_rflags:
    .zero 8
.size astraea_test_target_rflags, 8

.section .note.GNU-stack,"",@progbits
