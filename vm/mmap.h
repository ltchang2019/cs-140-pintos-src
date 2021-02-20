#ifndef VM_MMAP_H
#define VM_MMAP_H

#include <list.h>

/* Mapping ID that uniquely identifies a memory mapped file. */
typedef int mapid_t;

/* Memory mapped file. */
struct mmap_entry
  {
    mapid_t mapid;          /* Mapping ID. */
    void *uaddr;            /* User virtual address. */
    struct list_elem elem;  /* List element. */
  };

mapid_t mmap (int fd, void *addr);
void munmap (mapid_t mapid);
void munmap_all (void);

#endif /* vm/mmap.h */