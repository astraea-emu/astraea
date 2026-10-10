.text
.globl astraea_owned_client_entry
.type astraea_owned_client_entry, @function
astraea_owned_client_entry:
    movabs $42, %rdi
    # The owned fixture patches this RIP-relative address to its local GOT.
    call *0(%rip)
    ud2
.size astraea_owned_client_entry, .-astraea_owned_client_entry
.section .note.GNU-stack,"",@progbits
