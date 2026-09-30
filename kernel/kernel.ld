/* NOVA OS linker script (Phase 0 draft — provisional, untested).
 * Multiboot2 header section + entry symbol reserved here so Phase 1
 * has exactly one place defining the image layout.
 */
ENTRY(nova_entry)
SECTIONS {
    . = 1M;
    .text : { *(.multiboot2) *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(.bss*) *(COMMON) }
}
