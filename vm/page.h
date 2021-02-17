#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <debug.h>
#include <hash.h>
#include "filesys/file.h"

/* Location of a page. */
enum location
  {
    SWAP,     /* In a swap slot on the swap partition. */
    DISK,     /* Stored on disk. */
    ZERO,     /* A zero page. */
    FRAME     /* Page in a frame in physical memory. */
  };

/* Supplemental Page Table (SPT) entry. */
struct spte
  {
    void *page_uaddr;       /* User virtual address of the page. */
    enum location loc;      /* Location of the page. */
    struct file *file;      /* Reference to file if page on disk. */
    off_t offset;           /* Offset in file if page on disk. */
    struct hash_elem elem;  /* Hash element. */
  };

void spt_init (struct hash *hash_table);
void spt_insert (struct hash *spt, struct hash_elem *he);
void spt_delete (struct hash *spt, struct hash_elem *he);
void spt_free_table (struct hash *spt);

void spte_free (struct hash_elem *he, void *aux UNUSED);
struct spte *spte_create (void *page_addr, enum location loc, 
                          struct file* file, off_t offset);

#endif /* vm/page.h */
