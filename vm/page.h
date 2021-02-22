#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <debug.h>
#include <hash.h>
#include "filesys/file.h"

/* Page location/type used to determine what to do with the
   data in a page when it is first allocated, needs to be
   evicted, or needs to be freed. */
enum location
  {
    SWAP,  /* Pages that should be written to and read back from swap. */
    DISK,  /* Pages that should be written to and read back from disk. */
    MMAP,  /* Page for memory mapped file. */
    ZERO,  /* A zero page. */
    STACK  /* A stack page. */
  };

/* Supplemental page table (SPT) entry. */
struct spte
  {
    void *page_uaddr;       /* User virtual address of the page. */
    enum location loc;      /* Page location/type. */
    struct file *file;      /* Reference to file if location is DISK. */
    off_t ofs;              /* Offset in file if page location is DISK. */
    size_t swap_idx;        /* Index of swap slot if page location is SWAP. */
    size_t page_bytes;      /* Sequence of page data that is non-zero. */ 
    bool writable;          /* Indicates if page is writable or read only. */
    bool loaded;            /* Indicates if page has been loaded. */
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
