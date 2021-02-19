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
void munmap_pages (struct mmap_entry *entry);