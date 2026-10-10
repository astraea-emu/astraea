.text
.globl astraea_linked_client_entry
.type astraea_linked_client_entry,@function
astraea_linked_client_entry:
    movabs $42, %rdi
    call "ABCDEFGHIJK#owned-lib#owned-provider"@PLT
    ud2
.size astraea_linked_client_entry, .-astraea_linked_client_entry
.section .note.GNU-stack,"",@progbits
