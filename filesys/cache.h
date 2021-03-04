#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include "devices/block.h"
#include "threads/synch.h"

#define CACHE_SIZE 64
#define BLOCK_NOT_PRESENT SIZE_MAX

/* Sector type to distinguish between inodes and data. */
enum sector_type
  {
    INODE,  /* An inode sector. */
    DATA    /* A data sector. */
  };

/* Cache entry. */
struct cache_entry
  {
    enum sector_type type;      /* Sector type (inode or data). */
    block_sector_t sector_idx;  /* Sector number of disk location. */
    size_t cache_idx;           /* Index in the buffer cache. */
    bool dirty;                 /* Dirty flag for writes. */
    bool accessed;              /* Accessed flag for reads/writes. */
    struct rw_lock rw_lock;     /* Readers-writer lock. */
  };

/* Block sector element that allows block sector numbers
   to be placed in a list. */
struct sector_elem 
  {
    block_sector_t sector;  /* Sector number of disk location. */
    struct list_elem elem;  /* List element. */
  };

void *cache_idx_to_cache_slot (size_t cache_idx);
struct cache_entry *cache_idx_to_cache_entry (size_t cache_idx);
void cache_init (void);
size_t cache_get_block (block_sector_t sector, enum sector_type type);
void cache_flush (void);

void read_ahead_signal (block_sector_t sector);

#endif /* filesys/cache.h */
