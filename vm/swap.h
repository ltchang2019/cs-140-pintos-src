#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <stddef.h>

void swap_table_init (void);
size_t swap_write_page (const void *kpage);
void swap_read_page (void *kpage, size_t swap_idx);

#endif /* vm/swap.h */
