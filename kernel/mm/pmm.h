/* NOVA OS physical memory manager interface (Phase 2a).
 *
 * Two layers:
 *  Backend (exactly ONE of pmm_bitmap.c / pmm_buddy.c is linked): manages
 *  a single contiguous arena of 4KB frames, numbered 0..nframes-1 from
 *  base_addr. Metadata lives in caller-provided memory (carved from the
 *  arena itself by pmm.c, malloc'd by the host benchmark). Backends are
 *  pure C (stdint only): the same sources compile for guest and host.
 *  be_free has a caller contract: addr must be a live alloc (or ignored).
 *  be_alloc returns 0 on OOM (frame 0 is never allocated: arenas start
 *  above 1MB, so base_addr is never 0).
 *  Front (pmm.c, guest): arena selection from E820, metadata carving,
 *  stats, self-test.
 */
#ifndef NOVA_PMM_H
#define NOVA_PMM_H

#include <stdint.h>
#include "../../boot/boot_info.h"

#define NOVA_FRAME_SIZE 4096u

/* Backend API (implemented by the linked backend). */
uint32_t be_meta_needed(uint32_t nframes);
void be_init(uint32_t base_addr, uint32_t nframes, void *meta);
uint32_t be_alloc(void);
void be_free(uint32_t addr);
/* Contiguous runs (frames). Bitmap: real first-fit. Buddy: explicitly
 * unimplemented (returns 0 / ignores) — it is the parked alternative
 * (ADR-0010); the guest links bitmap. */
uint32_t be_alloc_contig(uint32_t nframes);
void be_free_contig(uint32_t addr, uint32_t nframes);
uint32_t be_free_count(void);
uint32_t be_largest_free(void);
const char *be_name(void);

/* Front API (guest). pmm_init carves backend metadata from the arena
 * itself when meta_buf is NULL (guest kernels); host tests pass a
 * malloc'd buffer (guest-physical carving would fault on the host). */
int pmm_init(const nova_boot_info_t *info, void *meta_buf,
             uint32_t meta_len);
uint32_t pmm_alloc_frame(void);
void pmm_free_frame(uint32_t addr);
uint32_t pmm_alloc_contig(uint32_t nframes);
void pmm_free_contig(uint32_t addr, uint32_t nframes);
uint32_t pmm_free_count(void);
uint32_t pmm_largest_free(void);
uint32_t pmm_total_frames(void);
uint32_t pmm_arena_base(void);
int pmm_selftest(void);

#endif /* NOVA_PMM_H */
