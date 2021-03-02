#include "filesys/cache.h"
#include <bitmap.h>
#include "threads/malloc.h"

/* Base addresses of cache, cache metadata, and cache bitmap. */
static void *cache;
static struct cache_entry *cache_metadata;
struct bitmap *cache_bitmap;

/* Lock on the cache for the eviction algorithm. */
static struct lock get_slot_lock;

/* Translates CACHE_IDX into address of the corresponding
   cache slot. */
void *
cache_idx_to_cache_slot (size_t cache_idx)
{
  ASSERT (cache_idx < CACHE_SIZE);

  size_t ofs = cache_idx * BLOCK_SECTOR_SIZE;
  void *slot = cache + ofs;

  return slot;
}

/* Allocates memory for cache, cache metadata, and cache
   bitmap. Initializes the global get_slot_lock for the
   eviction algorithm and individual rw_locks for each of
   the cache entries. Spawns two worker threads to handle
   read-ahead and write-behind for blocks in the cache. */
void
cache_init (void)
{
  /* Allocate memory. */
  cache = malloc (BLOCK_SECTOR_SIZE * CACHE_SIZE);
  cache_metadata = malloc (sizeof (struct cache_entry) * CACHE_SIZE);
  cache_bitmap = bitmap_create (CACHE_SIZE);
  if (cache == NULL || cache_metadata == NULL || cache_bitmap == NULL)
    PANIC ("cache_init: failed to allocate memory for cache data structures.");

  /* Initialize get_slot_lock. */
  lock_init (&get_slot_lock);

  /* Initialize fields including rw_lock for each cache_entry. */
  for (size_t idx = 0; idx < CACHE_SIZE; idx++)
    {
      struct cache_entry *ce = cache_metadata + idx;
      ce->sector_idx = SIZE_MAX;
      ce->cache_idx = SIZE_MAX;
      ce->dirty = false;
      ce->accessed = false;
      ce->inode = NULL;
      rw_lock_init (&ce->rw_lock);
    }
}
