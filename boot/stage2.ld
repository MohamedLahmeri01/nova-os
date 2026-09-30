/* NOVA OS stage2 (kernel) linker script (Phase 2b).
 * Two load sections so the VMM can enforce W^X per page:
 *   .text  @0x10000 : entry + code (mapped executable)
 *   .datax after ALIGN(4096) : rodata + data (mapped XD)
 * .bss is NOLOAD (not in the image); stage2asm zeroes [bss_start,
 * kernel_end) at entry. One objcopy call dumps .text+.datax with the
 * alignment gap padded; vmm_selftest proves the split at runtime.
 */
ENTRY(_stage2_start)
SECTIONS
{
  .text 0x10000 : {
    *(.text.boot)
    *(.text*)
  }
  . = ALIGN(4096);
  text_end = .;
  _text_end = .;
  .datax . : {
    *(.rdata* .rodata* .rodata.*)
    *(.data*)
  }
  . = ALIGN(4);
  .bss . (NOLOAD) : {
    bss_start = .;
    _bss_start = .;
    *(.bss*)
    *(COMMON)
    kernel_end = .;
    _kernel_end = .;
  }
  /DISCARD/ : {
    *(.eh_frame*) *(.comment*) *(.note*) *(.pdata*) *(.xdata*)
  }
}
