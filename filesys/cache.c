#include "filesys/cache.h"
#include "filesys/filesys.h"
#include <bitmap.h>
#include "devices/timer.h"
#include "threads/malloc.h"
#include "threads/thread.h"

/* Base addresses of cache, cache metadata, and cache bitmap. */
static void *cache;
static struct cache_entry *cache_metadata;
struct bitmap *cache_bitmap;

/* Lock on the cache for the eviction algorithm. */
static struct lock get_slot_lock;

/* List of block sectors to be pre-fetched. */
static struct list read_ahead_list;

/* Thread functions for asynchronous read-ahead and periodic
   writes of dirty blocks in cache back to disk. */
static thread_func cache_read_ahead NO_RETURN;
static thread_func cache_periodic_flush NO_RETURN;

/* Translates CACHE_IDX into address of the corresponding
   cache slot. */
void *
cache_idx_to_cache_slot (size_t cache_idx)
{
  ASSERT (cache_idx < CACHE_SIZE);

  size_t ofs = cache_idx * BLOCK_SECTOR_SIZE;
  void *cache_slot = cache + ofs;

  return cache_slot;
}

/* Allocates memory for cache, cache metadata, and cache
   bitmap. Initializes the global get_slot_lock for the
   eviction algorithm and individual rw_locks for each of
   the cache entries. Spawns two worker threads to handle
   asynchronous read-ahead and periodic writes of dirty
   blocks in the cache back to disk. */
void
cache_init (void)
{
  /* Allocate memory. */
  cache = malloc (BLOCK_SECTOR_SIZE * CACHE_SIZE);
  cache_metadata = malloc (sizeof (struct cache_entry) * CACHE_SIZE);
  cache_bitmap = bitmap_create (CACHE_SIZE);
  if (cache == NULL || cache_metadata == NULL || cache_bitmap == NULL)
    PANIC ("cache_init: failed memory allocation for cache data structures.");

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

  /* Initialize list of read-ahead block sectors and semaphore. */
  list_init (&read_ahead_list);
  sema_init (&read_ahead_sema, 0);

  /* Spawn worker threads for read-ahead and cache flushes. */
  tid_t tid_read_ahead = thread_create ("read-ahead", PRI_DEFAULT,
                                        cache_read_ahead, NULL);
  tid_t tid_periodic_flush = thread_create ("periodic-flush", PRI_DEFAULT,
                                            cache_periodic_flush, NULL);
  if (tid_read_ahead == TID_ERROR || tid_periodic_flush == TID_ERROR)
    PANIC ("cache_init: failed to spawn cache worker threads.");
}

/* Flushes the cache by writing all dirty blocks back to disk.
   
   The rw_lock for each cache_entry of a dirty block must be
   obtained through shared_acquire and we wait for the rw_lock
   for each dirty block rather than skipping over any. The dirty
   flag of each written block is then set back to false. */
void
cache_flush (void)
{
  for (size_t idx = 0; idx < CACHE_SIZE; idx++)
    {
      struct cache_entry *ce = cache_metadata + idx;
      if (ce->dirty)
        {
          rw_lock_shared_acquire (&ce->rw_lock);
          void *cache_slot = cache_idx_to_cache_slot (idx);
          block_write (fs_device, ce->sector_idx, cache_slot);
          ce->dirty = false;
          rw_lock_shared_release (&ce->rw_lock);
        }
    }
}

/* A thread function that automatically fetches the next block
   of a file into the cache when one block of a file is read.
  
   The read-ahead worker thread keeps track of a list of blocks
   to pre-fetch, and sleeps until signaled by another process
   that the pre-fetch list is non-empty. */
static void
cache_read_ahead (void *aux UNUSED)
{
  while (true)
    {
      sema_down (&read_ahead_sema);
      ASSERT (!list_empty (&read_ahead_list));
      
      struct list_elem *e = list_pop_front (&read_ahead_list);
      struct sector_elem *s = list_entry (e, struct sector_elem, elem);
      // FETCH BLOCK AT S->SECTOR
    }
}

/* A thread function that periodically writes all dirty blocks in
   the cache back to disk.
   
   The periodic-flush worker repeatedly sleeps for a specified
   amount of time (default 10 seconds) then wakes up and flushes
   the cache. */
static void
cache_periodic_flush (void *aux UNUSED)
{
  while (true)
    {
      timer_msleep (10 * 1000);
      // FLUSH CACHE;
    }
}
