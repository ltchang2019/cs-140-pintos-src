#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <debug.h>
#include <hash.h>
#include "filesys/file.h"

/* Page location/type used to determine what to do when
   a page needs to be evicted or read back into memory. */
enum location
  {
    SWAP,  /* Identifies pages that should be evicted to swap. */
    DISK,  /* Identifies pages that originate from disk. */
    ZERO,  /* A zero page. */
    STACK  /* A stack page. */
  };

/* Supplemental page table (SPT) entry. */
struct spte
  {
    void *page_uaddr;       /* User virtual address of the page. */
    enum location loc;      /* Page location/type. */
    struct file *file;      /* Reference to file location is DISK. */
    off_t ofs;              /* Offset in file if location is DISK. */
    size_t swap_idx;        /* Index of swap slot if location is SWAP. */
    size_t page_bytes;      /* Number of non-zero bytes in page. */
    bool writable;          /* Indicates whether page is writable. */
    bool loaded;            /* Indicates whether page has been loaded. */
    struct hash_elem elem;  /* Hash element. */
  };

void spt_init (struct hash *hash_table);
void spt_insert (struct hash *spt, struct hash_elem *he);
void spt_delete (struct hash *spt, struct hash_elem *he);
void spt_free_table (struct hash *spt);

struct spte *spte_create (void *page_uaddr, enum location loc,
                          struct file* file, off_t ofs, size_t swap_idx,
                          size_t page_bytes, bool writable, bool loaded);
struct spte *spte_lookup (void *page_uaddr);

#endif /* vm/page.h */
