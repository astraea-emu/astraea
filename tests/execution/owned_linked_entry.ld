/* A minimal complete host-linked ELF used only by the supervised test.
 * Its VMA is rebased to a free host guest range by an exact byte-policy.
 * No PS5 ABI, HLE or Sony provider identity is inferred.
 */
ENTRY(astraea_owned_linked_entry)
PHDRS {
    text PT_LOAD FLAGS(5);
}
SECTIONS {
    . = 0x1000;
    .text : { *(.text*) } :text
    /DISCARD/ : { *(.note*) *(.comment*) *(.eh_frame*) *(.gnu*) }
}
