.text
.globl astraea_owned_linked_entry
.type astraea_owned_linked_entry, @function
astraea_owned_linked_entry:
    # The test-owned initial context does not model a PS5 process ABI.
    movabs $42, %rdi
    # A sealed worker patches this owned address to its synthetic exit gate.
    movabs $0, %rax
    jmp *%rax
.size astraea_owned_linked_entry, .-astraea_owned_linked_entry
.section .note.GNU-stack,"",@progbits
