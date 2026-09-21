    .text
    .p2align 4
    .globl astraea_linux_enter_guest_context
    .type astraea_linux_enter_guest_context,@function
astraea_linux_enter_guest_context:
    # Save the requested guest RIP outside the register file. Native
    # execution v0 is serialized process-wide by the C++ backend.
    movq 128(%rdi), %rax
    movq %rax, .Lastraea_guest_rip_slot(%rip)

    # Keep the context pointer in R11 until every other guest register is
    # installed. R11 itself is loaded last.
    movq %rdi, %r11

    # Preserve host-only RFLAGS state while applying the synthetic guest
    # arithmetic/status subset (CF/PF/AF/ZF/SF/DF/OF). Bit 1 remains set.
    pushfq
    popq %rcx
    movq 136(%r11), %rdx
    andq $-3286, %rcx
    andq $3285, %rdx
    orq %rdx, %rcx
    orq $2, %rcx
    pushq %rcx
    popfq

    movq   8(%r11), %rbx
    movq  16(%r11), %rcx
    movq  24(%r11), %rdx
    movq  32(%r11), %rsi
    movq  48(%r11), %rbp
    movq  64(%r11), %r8
    movq  72(%r11), %r9
    movq  80(%r11), %r10
    movq  96(%r11), %r12
    movq 104(%r11), %r13
    movq 112(%r11), %r14
    movq 120(%r11), %r15
    movq   0(%r11), %rax
    movq  40(%r11), %rdi
    movq  56(%r11), %rsp
    movq  88(%r11), %r11

    jmp *.Lastraea_guest_rip_slot(%rip)
    .size astraea_linux_enter_guest_context, .-astraea_linux_enter_guest_context

    .section .bss
    .p2align 3
.Lastraea_guest_rip_slot:
    .quad 0

    .section .note.GNU-stack,"",@progbits
