#ifndef VM_MMAP_H
#define VM_MMAP_H

#include <list.h>

typedef int mapid_t;

struct mmap_entry
  {
    mapid_t mapid;
    void *uaddr;
    struct list_elem elem;
  };

mapid_t mmap (int fd, void *addr);
void munmap (mapid_t mapid);
void munmap_all (void);

#endif /* vm/mmap.h */