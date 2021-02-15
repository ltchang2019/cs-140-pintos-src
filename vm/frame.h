#ifndef VM_FRAME_H
#define VM_FRAME_H

struct frame
  {
     void *page_addr;
     // struct spte *spte;
     // struct lock frame_lock;
  };

void frame_table_init (size_t num_pages);
void *frame_alloc (enum palloc_flags flags);
void frame_free (void *page_addr);

#endif /* vm/frame.h */
