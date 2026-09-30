/* NOVA OS stage1 linker script (Phase 1).
 * Single flat section at 0x7C00; objcopy -O binary yields the 512-byte
 * boot sector body (signature appended by the image builder).
 * PE output from mingw ld; only section content lands in the binary.
 */
ENTRY(_start)
SECTIONS
{
  .text 0x7C00 : {
    *(.text*)
  }
  /DISCARD/ : {
    *(.eh_frame*) *(.comment*) *(.note*) *(.pdata*) *(.xdata*)
  }
}
