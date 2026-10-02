/* NOVA OS user program link script (Phase 6).
 * Base 0x80000000, everything merged into one section (code + rodata
 * + data + bss CONTENT, so file == memory image, no BSS fixup needed
 * by the loader). Entry _ustart first via .text.startup.
 */
ENTRY(_ustart)
SECTIONS
{
  .text 0x80000000 : {
    *(.text.startup)
    *(.text*)
    *(.rdata* .rodata* .rodata.*)
    *(.data*)
    *(.bss*)
    *(COMMON)
  }
  /DISCARD/ : {
    *(.eh_frame*) *(.comment*) *(.note*) *(.pdata*) *(.xdata*)
  }
}
ASSERT(_ustart == 0x80000000, "init entry must be first");
