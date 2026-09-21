option casemap:none

.data?
align 8
astraea_windows_guest_rip_slot QWORD ?

.code

PUBLIC astraea_windows_enter_guest_context
astraea_windows_enter_guest_context PROC
    ; RCX = GuestCpuContext*, RDX = WindowsTransitionFrame*.
    ; Save all Windows x64 nonvolatile host state before guest clobber.
    mov qword ptr [rdx + 0], rsp
    mov qword ptr [rdx + 8], rbx
    mov qword ptr [rdx + 16], rbp
    mov qword ptr [rdx + 24], rsi
    mov qword ptr [rdx + 32], rdi
    mov qword ptr [rdx + 40], r12
    mov qword ptr [rdx + 48], r13
    mov qword ptr [rdx + 56], r14
    mov qword ptr [rdx + 64], r15

    movdqu xmmword ptr [rdx + 72], xmm6
    movdqu xmmword ptr [rdx + 88], xmm7
    movdqu xmmword ptr [rdx + 104], xmm8
    movdqu xmmword ptr [rdx + 120], xmm9
    movdqu xmmword ptr [rdx + 136], xmm10
    movdqu xmmword ptr [rdx + 152], xmm11
    movdqu xmmword ptr [rdx + 168], xmm12
    movdqu xmmword ptr [rdx + 184], xmm13
    movdqu xmmword ptr [rdx + 200], xmm14
    movdqu xmmword ptr [rdx + 216], xmm15
    stmxcsr dword ptr [rdx + 232]
    fnstcw word ptr [rdx + 236]

    ; Keep the portable context pointer in R11 until guest R11 loads last.
    mov r11, rcx

    mov rax, qword ptr [r11 + 128]
    mov qword ptr [astraea_windows_guest_rip_slot], rax

    ; Preserve host-only RFLAGS state while applying the synthetic guest
    ; arithmetic/status subset. DF/TF are rejected by the C++ validator.
    pushfq
    pop r8
    mov r9, qword ptr [r11 + 136]
    and r8, -3286
    and r9, 3285
    or r8, r9
    or r8, 2
    push r8
    popfq

    mov rbx, qword ptr [r11 + 8]
    mov rcx, qword ptr [r11 + 16]
    mov rdx, qword ptr [r11 + 24]
    mov rsi, qword ptr [r11 + 32]
    mov rdi, qword ptr [r11 + 40]
    mov rbp, qword ptr [r11 + 48]
    mov r8, qword ptr [r11 + 64]
    mov r9, qword ptr [r11 + 72]
    mov r10, qword ptr [r11 + 80]
    mov r12, qword ptr [r11 + 96]
    mov r13, qword ptr [r11 + 104]
    mov r14, qword ptr [r11 + 112]
    mov r15, qword ptr [r11 + 120]
    mov rax, qword ptr [r11 + 0]
    mov rsp, qword ptr [r11 + 56]
    mov r11, qword ptr [r11 + 88]

    jmp qword ptr [astraea_windows_guest_rip_slot]
astraea_windows_enter_guest_context ENDP

PUBLIC astraea_windows_recover_guest_context
astraea_windows_recover_guest_context PROC
    ; RCX = WindowsTransitionFrame*. No calls are made on guest state.
    ldmxcsr dword ptr [rcx + 232]
    fldcw word ptr [rcx + 236]

    movdqu xmm6, xmmword ptr [rcx + 72]
    movdqu xmm7, xmmword ptr [rcx + 88]
    movdqu xmm8, xmmword ptr [rcx + 104]
    movdqu xmm9, xmmword ptr [rcx + 120]
    movdqu xmm10, xmmword ptr [rcx + 136]
    movdqu xmm11, xmmword ptr [rcx + 152]
    movdqu xmm12, xmmword ptr [rcx + 168]
    movdqu xmm13, xmmword ptr [rcx + 184]
    movdqu xmm14, xmmword ptr [rcx + 200]
    movdqu xmm15, xmmword ptr [rcx + 216]

    mov rbx, qword ptr [rcx + 8]
    mov rbp, qword ptr [rcx + 16]
    mov rsi, qword ptr [rcx + 24]
    mov rdi, qword ptr [rcx + 32]
    mov r12, qword ptr [rcx + 40]
    mov r13, qword ptr [rcx + 48]
    mov r14, qword ptr [rcx + 56]
    mov r15, qword ptr [rcx + 64]
    mov rsp, qword ptr [rcx + 0]
    ret
astraea_windows_recover_guest_context ENDP

END
