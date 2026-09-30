/* NOVA OS boot-time memory layout (Phase 2b).
 * Asm-compatible: preprocessor #defines only (included from stage1.S
 * via -I boot and from C). Single source of truth — no magic numbers
 * duplicated across boot and kernel sources.
 */
#ifndef NOVA_MEM_LAYOUT_H
#define NOVA_MEM_LAYOUT_H

/* Real-mode / early-pmode landmarks (all < 1MB, all identity-mapped). */
#define NOVA_BOOT_INFO_AT 0x500
#define NOVA_E820_BUF 0x5000
#define NOVA_KERNEL_LOAD 0x10000
#define NOVA_STACK_TOP 0x90000
#define NOVA_STACK_SIZE 0x4000
#define NOVA_STACK_GUARD (NOVA_STACK_TOP - NOVA_STACK_SIZE - 0x1000)

/* Arena policy: everything at/above 1MB usable if marked type-1. */
#define NOVA_ARENA_MIN 0x100000

#endif /* NOVA_MEM_LAYOUT_H */
