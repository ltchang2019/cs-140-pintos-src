#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <debug.h>
#include <hash.h>
#include "filesys/file.h"

enum location 
  {
    SWAP,
    DISK,
    ZERO
  };

struct spte
  {
    int id;
    enum location loc;
    struct file *file;
    off_t offset;
    struct hash_elem elem;
  };

void init_spt (struct hash *hash_table);

#endif /* vm/page.h */
