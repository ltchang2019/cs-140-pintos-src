#include "filesys/cache.h"
#include <bitmap.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "devices/timer.h"
#include "threads/malloc.h"
#include "threads/thread.h"

/* Base addresses for cache data structures. */
static void *cache;
static struct cache_entry *cache_metadata;
static struct bitmap *cache_bitmap;

/* Lock and condition variable for block eviction. */
static struct lock eviction_lock;
static struct condition eviction_cond;

/* Clock hands for the eviction algorithm. */
static struct cache_entry *lagging_hand;
static struct cache_entry *leading_hand;

/* A semaphore to signal the read-ahead worker thread. */
static struct semaphore read_ahead_sema;

/* List of block sectors to be pre-loaded into cache. */
static struct list read_ahead_list;

/* Thread functions for asynchronous read-ahead and
   periodic writes of dirty blocks back to disk. */
static thread_func cache_read_ahead NO_RETURN;
static thread_func cache_periodic_flush NO_RETURN;

static void clock_advance (void);
static size_t clock_find (void);
static size_t cache_evict_block (void);
static size_t cache_find_block (block_sector_t sector);
static size_t cache_load (block_sector_t sector);

/* Initializes the buffer cache.

   More specifically, allocates memory for cache, cache
   metadata, and cache bitmap. Initializes the global
   eviction_lock for the eviction algorithm and individual
   rw_locks for each of the cache_entry structs. Spawns
   two worker threads to handle asynchronous read-ahead
   and periodic writes of dirty blocks back to disk. */
void
cache_init (void)
{
  /* Allocate memory. */
  cache = malloc (BLOCK_SECTOR_SIZE * CACHE_SIZE);
  cache_metadata = malloc (sizeof (struct cache_entry) * CACHE_SIZE);
  cache_bitmap = bitmap_create (CACHE_SIZE);
  if (cache == NULL || cache_metadata == NULL || cache_bitmap == NULL)
    PANIC ("cache_init: failed memory allocation for cache data structures.");

  /* Initialize eviction_lock and eviction condition variable. */
  lock_init (&eviction_lock);
  cond_init (&eviction_cond);

  /* Initialize fields for each cache_entry. */
  for (size_t idx = 0; idx < CACHE_SIZE; idx++)
    {
      struct cache_entry *ce = cache_metadata + idx;
      ce->sector_idx = SECTOR_NOT_PRESENT;
      ce->cache_idx = idx;
      ce->dirty = false;
      ce->accessed = false;
      rw_lock_init (&ce->rw_lock);
    }

  /* Initialize clock hands for eviction algorithm. */
  lagging_hand = cache_metadata;
  leading_hand = cache_metadata + (CACHE_SIZE / 4);

  /* Initialize list and semaphore for read-ahead worker thread. */
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

/* Translates CACHE_IDX into address of the corresponding
   cache slot in the cache. */
void *
cache_idx_to_cache_block_addr (size_t cache_idx)
{
  return cache + (cache_idx * BLOCK_SECTOR_SIZE);
}

/* Translates CACHE_IDX into address of the corresponding
   cache_entry in the cache_metadata. */
struct cache_entry *
cache_idx_to_cache_entry (size_t cache_idx)
{
  return cache_metadata + cache_idx;
}

/* Get a block with sector number SECTOR and inode type
   TYPE into memory by locating it in the cache or loading
   it from disk.
   
   On function return, the rw_lock of the cache slot is
   held in exclusive_acquire mode. */
void *
cache_get_block_exclusive (block_sector_t sector, enum inode_type type)
{
  size_t cache_idx = cache_get_block (sector, type);
  struct cache_entry *ce = cache_metadata + cache_idx;
  rw_lock_shared_to_exclusive (&ce->rw_lock);

  /* Always going to set dirty when acquiring in exclusive mode. */
  ce->dirty = true;
  return cache_idx_to_cache_block_addr (cache_idx);
}

/* Get a block with sector number SECTOR and inode type
   TYPE into memory by locating it in the cache or loading
   it from disk.
   
   On function return, the rw_lock of the cache slot is
   held in shared_acquire mode. */
void *
cache_get_block_shared (block_sector_t sector, enum inode_type type)
{
  size_t cache_idx = cache_get_block (sector, type);
  return cache_idx_to_cache_block_addr (cache_idx);
}

/* Release exclusive hold on the rw_lock of the cache slot
   with address BLOCK_ADDR. */
void
cache_exclusive_release (void *block_addr)
{
  ASSERT ((block_addr - cache) % BLOCK_SECTOR_SIZE == 0);

  size_t cache_idx = (block_addr - cache) / BLOCK_SECTOR_SIZE;
  struct cache_entry *ce = cache_metadata + cache_idx;
  rw_lock_exclusive_release (&ce->rw_lock);
}

/* Release shared hold on the rw_lock of the cache slot
   with address BLOCK_ADDR. */
void 
cache_shared_release (void *block_addr)
{
  ASSERT ((block_addr - cache) % BLOCK_SECTOR_SIZE == 0);

  size_t cache_idx = (block_addr - cache) / BLOCK_SECTOR_SIZE;
  struct cache_entry *ce = cache_metadata + cache_idx;
  rw_lock_shared_release (&ce->rw_lock);
}

/* Convert shared hold on the rw_lock of the cache slot
   with address BLOCK_ADDR to exclusive hold. */
void 
cache_shared_to_exclusive (void *block_addr)
{
  ASSERT ((block_addr - cache) % BLOCK_SECTOR_SIZE == 0);

  size_t cache_idx = (block_addr - cache) / BLOCK_SECTOR_SIZE;
  struct cache_entry *ce = cache_idx_to_cache_entry (cache_idx);
  rw_lock_shared_to_exclusive (&ce->rw_lock);
}

/* Convert exclusive hold on the rw_lock of the cache slot
   with address BLOCK_ADDR to shared hold. */
void
cache_exclusive_to_shared (void *block_addr)
{
  ASSERT ((block_addr - cache) % BLOCK_SECTOR_SIZE == 0);

  size_t cache_idx = (block_addr - cache) / BLOCK_SECTOR_SIZE;
  struct cache_entry *ce = cache_idx_to_cache_entry (cache_idx);
  rw_lock_exclusive_to_shared (&ce->rw_lock);
}

/* Release the hold on the rw_lock of the cache slot with
   address BLOCK_ADDR appropriately, with shared_release
   if EXCLUSIVE is false and exclusive_release otherwise. */
void
cache_conditional_release (void *block_addr, bool exclusive)
{
  if (exclusive)
    cache_exclusive_release (block_addr);
  else
    cache_shared_release (block_addr);
}

/* Get a block with sector number SECTOR into memory by
   locating it in the cache or loading it from disk.

   Upon return, the rw_lock of the cache_entry for the
   cache slot will be held in shared_acquire mode. It is
   the caller's responsibility to release the rw_lock or
   upgrade it to exclusive_acquire if necessary. */
size_t
cache_get_block (block_sector_t sector, enum sector_type type)
{
  size_t cache_idx = cache_load (sector);
  struct cache_entry *ce = cache_metadata + cache_idx;
  ce->type = type;
  ce->sector_idx = sector;
  ce->accessed = true;

  return cache_idx;
}

/* Free the cache slot containing a block with sector
   number SECTOR, and reset the fields of the corresponding
   cache_entry. If the block with sector number SECTOR is
   not in the cache, nothing needs to be done.
   
   If the block is in the cache, cache_find_block() will
   return with a rw_lock in shared_acquire mode on the
   cache slot. Release this rw_lock after resetting fields
   and before returning to caller. */
void
cache_free_slot (block_sector_t sector)
{
  size_t cache_idx = cache_find_block (sector);
  if (cache_idx == BLOCK_NOT_PRESENT)
    return;
  
  struct cache_entry *ce = cache_metadata + cache_idx;
  ce->sector_idx = SECTOR_NOT_PRESENT;
  ce->dirty = false;
  ce->accessed = false;
  
  bitmap_reset (cache_bitmap, cache_idx);
  rw_lock_shared_release (&ce->rw_lock);
}

/* Flushes cache by writing all dirty blocks back to disk.
   
   The rw_lock for each cache_entry of a dirty block must
   be obtained through shared_acquire and we wait for the
   rw_lock for each dirty block rather than skipping. */
void
cache_flush (void)
{
  for (size_t idx = 0; idx < CACHE_SIZE; idx++)
    {
      struct cache_entry *ce = cache_metadata + idx;

      rw_lock_shared_acquire (&ce->rw_lock);
      if (ce->sector_idx != SECTOR_NOT_PRESENT && ce->dirty)
        {
          void *cache_block_addr = cache_idx_to_cache_block_addr (idx);
          block_write (fs_device, ce->sector_idx, cache_block_addr);
        }
      rw_lock_shared_release (&ce->rw_lock);
    }
}

/* Find a block in the cache to evict using the second
   chance clock algorithm. Returns the cache_idx of the
   slot occupied by the block to be evicted.
   
   On function return, the rw_lock for the chosen cache
   slot will be held in exclusive_acquire mode. It is the
   caller's responsibility to release it. */
static size_t
clock_find (void)
{
  ASSERT (lock_held_by_current_thread (&eviction_lock));

  while (true)
    {
      if (rw_lock_shared_try_acquire (&lagging_hand->rw_lock))
        {
          if (!lagging_hand->accessed &&
              lagging_hand->rw_lock.active_readers == 1)
            {
              rw_lock_shared_to_exclusive (&lagging_hand->rw_lock);
              size_t cache_idx = lagging_hand->cache_idx;
              clock_advance ();

              return cache_idx;
            }
          rw_lock_shared_release (&lagging_hand->rw_lock);
        }
        
      /* Advance clock hand. */
      clock_advance ();
    }

  NOT_REACHED ();
}

/* Advance the hands of the clock algorithm by one cache
   slot, wrapping around to the first slot if the end of
   the cache is reached for either hand. */
static void
clock_advance (void)
{
  if (++lagging_hand >= cache_metadata + CACHE_SIZE)
    lagging_hand = cache_metadata;
  if (++leading_hand >= cache_metadata + CACHE_SIZE)
    leading_hand = cache_metadata;
  
  leading_hand->accessed = false;
}

/* Evicts a block from a cache slot it is in and returns
   the cache_idx of the free cache slot. If the evicted
   block is dirty, it is written back to disk. 
   
   The rw_lock of the cache slot is held in shared_acquire
   mode after this function returns. */
static size_t
cache_evict_block (void)
{
  size_t cache_idx = clock_find ();
  struct cache_entry *ce = cache_metadata + cache_idx;
  
  /* Write dirty block back to disk. */
  if (ce->dirty)
    {
      void *cache_block_addr = cache_idx_to_cache_block_addr (cache_idx);
      block_write (fs_device, ce->sector_idx, cache_block_addr);
    }
  
  /* Clear appropriate fields in cache_entry. */
  ce->sector_idx = SECTOR_NOT_PRESENT;
  ce->dirty = false;
  ce->accessed = false;

  /* Atomically convert exclusive_acquire on rw_lock to
     shared_acquire so that all paths through cache_load()
     return with the rw_lock in shared_acquire mode. */
  rw_lock_exclusive_to_shared (&ce->rw_lock);

  return cache_idx;
}

/* Searches the cache to see if a block with sector number
   SECTOR is already loaded. If yes, the rw_lock of the
   cache_entry is obtained via shared_acquire, and the
   cache_idx of the loaded block is returned. Otherwise,
   return an error value indicating the requested block
   is not present in the cache. */
static size_t
cache_find_block (block_sector_t sector)
{
  for (size_t idx = 0; idx < CACHE_SIZE; idx++)
    {
      struct cache_entry *ce = cache_metadata + idx;
      if (ce->sector_idx == sector)
        {
          while (!rw_lock_shared_try_acquire (&ce->rw_lock))
            cond_wait (&eviction_cond, &eviction_lock);

          /* If block still contains correct disk sector,
             return given cache_idx. Else, restart search. */
          if (ce->sector_idx == sector)
            return ce->cache_idx;
          else
            idx = 0;
        }
    }

  return BLOCK_NOT_PRESENT;
}

/* Find a block with sector number SECTOR in the cache and
   return the cache_idx of the slot it is in, or load the
   block from disk into a slot if it isn't already in the
   cache. Evict a block from the cache if necessary. 
   
   The rw_lock of the cache slot is held in shared_acquire
   mode after this function returns. */
static size_t
cache_load (block_sector_t sector)
{
  size_t cache_idx;
  struct cache_entry *ce;
  lock_acquire (&eviction_lock);

  /* Block already in cache, so just return the cache_idx. */
  cache_idx = cache_find_block (sector);
  if (cache_idx != BLOCK_NOT_PRESENT)
    {
      cond_broadcast (&eviction_cond, &eviction_lock);
      lock_release (&eviction_lock);
      return cache_idx;
    }

  /* Block not in cache, so find a free slot and load it in. */
  cache_idx = bitmap_scan_and_flip (cache_bitmap, 0, 1, false);

  /* A free cache slot is available, so obtain the rw_lock on
     the cache_entry and set the sector_idx field. */
  ce = cache_metadata + cache_idx;
  if (cache_idx != BITMAP_ERROR && 
      rw_lock_shared_try_acquire (&ce->rw_lock))
    {
      cond_broadcast (&eviction_cond, &eviction_lock);
      lock_release (&eviction_lock);

      void *cache_block_addr = cache_idx_to_cache_block_addr (cache_idx);
      block_read (fs_device, sector, cache_block_addr);
      return cache_idx;
    }

  /* Cache is full, so evict a block from a cache slot to
     obtain a free slot for the new block. Can release
     eviction_lock since we will have shared lock on the
     returned cache slot, ensuring that the block will not
     be evicted by another process. */
  cache_idx = cache_evict_block ();
  cond_broadcast (&eviction_cond, &eviction_lock);
  lock_release (&eviction_lock);

  void *cache_block_addr = cache_idx_to_cache_block_addr (cache_idx);
  block_read (fs_device, sector, cache_block_addr);
  return cache_idx;
}

/* Signals the read-ahead worker thread that a block has
   been enqueued to be loaded into the cache. */
void
read_ahead_signal (block_sector_t sector)
{
  struct sector_elem *se = malloc (sizeof (struct sector_elem));
  if (se == NULL)
    PANIC ("read_ahead_signal: memory allocation failed for sector_elem.");

  se->sector = sector;
  list_push_back (&read_ahead_list, &se->elem);
  sema_up (&read_ahead_sema);
}

/* A thread function that fetches the next block of a file
   into the cache when one block of a file is read.
  
   The read-ahead worker thread keeps track of a list of
   blocks to fetch, and sleeps until signaled by another
   process that the list is non-empty. */
static void
cache_read_ahead (void *aux UNUSED)
{
  while (true)
    {
      sema_down (&read_ahead_sema);
      ASSERT (!list_empty (&read_ahead_list));
      
      struct list_elem *e = list_pop_front (&read_ahead_list);
      struct sector_elem *se = list_entry (e, struct sector_elem, elem);
    
      size_t cache_idx = cache_get_block (se->sector, DATA);
      struct cache_entry *ce = cache_metadata + cache_idx;
      rw_lock_shared_release (&ce->rw_lock);

      /* Deallocate memory for sector elem. */
      free (se);
    }
}

/* A thread function that periodically writes the free map
   and all dirty blocks in the cache back to disk.
   
   The periodic-flush worker repeatedly sleeps for a
   specified amount of time (default 10 seconds) then
   wakes up and flushes the free map and cache. */
static void
cache_periodic_flush (void *aux UNUSED)
{
  while (true)
    {
      timer_msleep (10 * 1000);
      cache_flush ();
      free_map_flush ();
    }
}
