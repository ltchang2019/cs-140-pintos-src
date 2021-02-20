#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/palloc.h"
#include "threads/synch.h"
#include "vm/page.h"

/* Frame table entry. */
struct frame_entry
  {
     void *page_kaddr;     /* Kernel virtual address of page in frame. */
     struct spte *spte;    /* Reference to SPT entry for page in frame. */
     struct thread *thread;
     struct lock lock;     /* Mutual exclusion for frames. */
  };

void frame_table_init (size_t num_frames);
void *frame_alloc_page (enum palloc_flags flags, struct spte *spte);
void frame_free_page (void *page_kaddr);

#endif /* vm/frame.h */