#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include "devices/block.h"
#include "threads/synch.h"

#define CACHE_SIZE 64

/* Sector type to distinguish between inodes and data. */
enum sector_type
  {
    INODE,  /* An inode sector. */
    DATA    /* A data sector. */
  };

/* Cache entry. */
struct cache_entry
  {
    enum sector_type type;      /* Sector type. */
    block_sector_t sector_idx;  /* Sector number of disk location. */
                                // IS THIS NECESSARY? CAN BE OBTAINED
                                // FROM INODE->SECTOR
    size_t cache_idx;           /* Location in the buffer cache. */
    bool dirty;                 /* Dirty flag for writes. */
    bool accessed;              /* Accessed flag for reads/writes. */
    struct inode *inode;        /* Reference to in-memory inode. */
    struct rw_lock rw_lock;     /* Readers-writer lock for the block. */
  };

void *cache_idx_to_cache_slot (size_t cache_idx);
void cache_init (void);

#endif /* filesys/inode.h */