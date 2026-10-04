/* NOVA OS VGA text driver interface (Phase 8d, console).
 * 80x25 color text at 0xB8000 (always addressable: real-mode low
 * memory, identity-mapped later). Attribute 0x07 (gray on black).
 * Handles \\r \\n \\b + printable; other bytes print as-is (no
 * escape sequences in v0.1). Cursor via CRT 0x3D4/0x3D5.
 */
#ifndef NOVA_VGA_H
#define NOVA_VGA_H

/* Clear screen, cursor home. Safe any time after boot. */
void vga_init(void);
/* One char to the screen (mirrored from serial_putc). */
void vga_putc(char c);
/* Deterministic selftest: cell write/readback + cursor round-trip.
 * Prints VGA-OK. Nonzero on failure (fatal). */
int vga_selftest(void);

#endif /* NOVA_VGA_H */
