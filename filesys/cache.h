#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include "devices/block.h"
#include "threads/synch.h"

#define CACHE_SIZE 64
#define CACHE_NOT_PRESENT SIZE_MAX

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
    size_t cache_idx;           /* Location in the buffer cache. */
    bool dirty;                 /* Dirty flag for writes. */
    bool accessed;              /* Accessed flag for reads/writes. */
                                // IS ACCESSED FLAG NECESSARY?
    struct inode *inode;        /* Reference to in-memory inode. */
    struct rw_lock rw_lock;     /* Readers-writer lock for the block. */
  };

/* A semaphore to signal the read-ahead worker thread. */
struct semaphore read_ahead_sema;

/* Block sector element that allows block sector numbers
   to be placed in a list. */
struct sector_elem 
  {
    block_sector_t sector;  /* Sector number of disk location. */
    struct list_elem elem;  /* List element. */
  };

void *cache_idx_to_cache_slot (size_t cache_idx);

void cache_init (void);
size_t cache_load (block_sector_t sector);
void cache_flush (void);

#endif /* filesys/cache.h */