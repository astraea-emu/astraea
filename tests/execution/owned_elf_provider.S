.text
.globl astraea_owned_provider_entry
.type astraea_owned_provider_entry, @function
astraea_owned_provider_entry:
    # The owned fixture patches this immediate to its synthetic exit gate.
    movabs $0, %rax
    jmp *%rax
.size astraea_owned_provider_entry, .-astraea_owned_provider_entry
.section .note.GNU-stack,"",@progbits
