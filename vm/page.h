#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <debug.h>
#include <hash.h>
#include "filesys/file.h"

typedef enum 
  {
    SWAP,
    DISK,
    ZERO
  } location;

struct spte
  {
    void *page_addr;
    location loc;
    struct file *file;
    off_t offset;
    struct hash_elem elem;
  };

void spt_init (struct hash *hash_table);
void spt_insert (struct hash *spt, struct hash_elem *he);
void spt_delete (struct hash *spt, struct hash_elem *he);
void spt_free_table (struct hash *spt);

void spte_free (struct hash_elem *he, void *aux UNUSED);
struct spte *spte_create (void *page_addr, 
                          location loc, 
                          struct file* file, 
                          off_t offset);

#endif /* vm/page.h */
