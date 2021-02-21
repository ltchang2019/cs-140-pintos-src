#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <stddef.h>

/* Default value that indicates a page is not in swap. */
#define SWAP_DEFAULT SIZE_MAX

void swap_table_init (void);
size_t swap_write_page (const void *kpage);
void swap_read_page (void *kpage, size_t swap_idx);
void swap_free_slot (size_t swap_idx);

#endif /* vm/swap.h */
