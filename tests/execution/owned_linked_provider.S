.text
.globl astraea_linked_provider_entry
.type astraea_linked_provider_entry,@function
.globl "ABCDEFGHIJK#owned-lib#owned-provider"
.type "ABCDEFGHIJK#owned-lib#owned-provider",@function
astraea_linked_provider_entry:
"ABCDEFGHIJK#owned-lib#owned-provider":
    # The owned fixture substitutes one synthetic exit-gate address.
    movabs $0, %rax
    jmp *%rax
.size astraea_linked_provider_entry, .-astraea_linked_provider_entry
.size "ABCDEFGHIJK#owned-lib#owned-provider", .-"ABCDEFGHIJK#owned-lib#owned-provider"
.section .note.GNU-stack,"",@progbits
