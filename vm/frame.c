#include "vm/frame.h"
#include <debug.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "vm/swap.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"

/* Base addresses used to index into frame table. */
static void *user_pool_base;
static struct frame_entry *frame_table_base;

/* Second chance clock algorithm for page eviction. */
static struct frame_entry *lead_hand;
static struct frame_entry *lag_hand;
static size_t free_frames;
static struct lock clock_lock;

static struct frame_entry *page_kaddr_to_frame_addr (void *page_kaddr);
static void *frame_evict_page (void);
static struct frame_entry *clock_find_frame (void);
static void clock_advance (void);

/* Translates address returned by palloc_get_page() into
   address of corresponding frame in frame_table. */
struct frame_entry *
page_kaddr_to_frame_addr (void *page_kaddr)
{
  ASSERT (user_pool_base != NULL && page_kaddr != NULL);

  size_t index = (page_kaddr - user_pool_base) >> PGBITS;
  struct frame_entry *frame_addr = frame_table_base + index;

  return frame_addr;
}

/* Allocates memory for frame table and gets user_pool_base
   address for use in page_kaddr -> frame_addr translation.
   Allocates space for at most NUM_FRAMES frames. */
void
frame_table_init (size_t num_frames)
{
  /* Free memory starts at 1 MB and runs to the end of RAM.
     Half of free memory can be used to store frames. */
  uint8_t *free_start = ptov (1024 * 1024);
  uint8_t *free_end = ptov (init_ram_pages * PGSIZE);
  free_frames = (free_end - free_start) / (PGSIZE * 2);
  if (free_frames > num_frames)
    free_frames = num_frames;

  /* Initialize frames in frame table. */
  frame_table_base = malloc (sizeof (struct frame_entry) * free_frames);
  if (frame_table_base == NULL)
    PANIC ("Not enough memory for frame table.");

  for (size_t idx = 0; idx < free_frames; idx++)
    {
      struct frame_entry *f = frame_table_base + idx;
      f->page_kaddr = NULL;
      f->spte = NULL;
      f->thread = NULL;
      lock_init (&f->lock);
    }

  lock_init (&clock_lock);

  /* Get base address of the user pool. */
  user_pool_base = palloc_get_user_pool_base ();

  /* Initialize leading and lagging hands. */
  lead_hand = frame_table_base;
  lag_hand = frame_table_base + (free_frames / 2);
}

/* Obtain a page from the user pool and store it in the next
   available frame. Depending on the fields in the supplemental
   page table entry for the page, fill the page with the
   appropriate data. Return NULL if unsuccessful. */
void *
frame_alloc_page (enum palloc_flags flags, struct spte *spte)
{
  ASSERT (flags & PAL_USER);

  /* Get a page of memory. Evict a page if necessary. */
  void *page_kaddr = palloc_get_page (flags);
  if (page_kaddr == NULL)
    {
      page_kaddr = frame_evict_page ();
      ASSERT (page_kaddr != NULL);
    }

  /* Get index to available frame and set fields in frame. */
  struct frame_entry *f = page_kaddr_to_frame_addr (page_kaddr);
  f->page_kaddr = page_kaddr; // potentially redundant if evicted frame
  lock_acquire (&f->lock);
  f->spte = spte;
  f->thread = thread_current ();

  /* Load data into the page depending on it's location/type. */
  if (spte->loc == ZERO || spte->loc == STACK)
    memset (page_kaddr, 0, PGSIZE);
  else if (spte->loc == SWAP && !spte->loaded)
    swap_read_page (page_kaddr, spte->swap_idx);
  else if (spte->loc == DISK && !spte->loaded)
    {
      /* Read the non-zero bytes of the page from the file on disk
         and zero the remaining bytes. */
      lock_acquire (&filesys_lock);
      off_t bytes_read = file_read_at (spte->file, page_kaddr,
                                       spte->page_bytes, spte->ofs);
      lock_release (&filesys_lock);
      if (bytes_read != (int) spte->page_bytes)
        {
          palloc_free_page (page_kaddr);
          return NULL;
        }
      memset (page_kaddr + bytes_read, 0, PGSIZE - bytes_read);
    }

  spte->loaded = true;
  lock_release (&f->lock);
  return page_kaddr;
}

/* Remove a page with kernel virtual address PAGE_KADDR from
   it's frame and free the page. */
void
frame_free_page (void *page_kaddr)
{
  struct frame_entry *f = page_kaddr_to_frame_addr (page_kaddr);
  ASSERT (f != NULL);
  
  lock_acquire (&f->lock);
  pagedir_clear_page(thread_current ()->pagedir, f->spte->page_uaddr);
  f->page_kaddr = NULL;
  f->thread = NULL;
  f->spte = NULL;
  lock_release (&f->lock);

  palloc_free_page (page_kaddr);
}

static struct frame_entry *
clock_find_frame (void)
{
  ASSERT (lock_held_by_current_thread (&clock_lock));

  struct frame_entry *evict_frame;
  while (true)
    {
      /* If frame was freed since clock algorithm began.
         (i.e. we enter frame_evict because no page available,
          get interrupted by another process that frees frame,
          then see conditions below. */
      if (lag_hand->thread == NULL && 
          lock_try_acquire (&lag_hand->lock))
        {
          evict_frame = lag_hand;
          break;
        }  
      
      /* Frame is evictable. */
      if (!pagedir_is_accessed (lag_hand->thread->pagedir,    
                                lag_hand->spte->page_uaddr) &&
          lock_try_acquire (&lag_hand->lock))
        {
          evict_frame = lag_hand;
          break;
        }

      /* This condition is HACKY. */
      if(lead_hand->thread != NULL)
        pagedir_set_accessed (lead_hand->thread->pagedir, 
                            lead_hand->spte->page_uaddr, false);
      clock_advance ();
    }
  
  /* Advance clock one more time so next frame_evict() call
     doesn't evict just-allocated frame. */
  clock_advance ();
  return evict_frame;
}

static void
clock_advance (void)
{
  ASSERT (lock_held_by_current_thread (&clock_lock));
  
  ++lead_hand, ++lag_hand;
  if (lead_hand == frame_table_base + free_frames)
    lead_hand = frame_table_base;
  if (lag_hand == frame_table_base + free_frames)
    lag_hand = frame_table_base;
}

/* Evict a page from it's frame and return the kernel virtual
   address that is now free to be used by another process. */
static void *
frame_evict_page (void)
{
  lock_acquire (&clock_lock);
  struct frame_entry *f = clock_find_frame ();
  ASSERT (lock_held_by_current_thread (&f->lock));
  lock_release (&clock_lock);
  
  /* CURRENTLY DOES NOT IMPLEMENT SECOND CHANCE CLOCK ALGORITHM. */
  // struct frame_entry *f = lead_hand;
  
  /* Increment leading hand for page eviction. */
  // lead_hand = lead_hand + 10;
  // if (lead_hand >= frame_table_base + free_frames)
  //   lead_hand = frame_table_base;

  // lock_acquire (&f->lock);
  struct thread *t = f->thread;
  struct spte *spte = f->spte;

  /* Write current page in frame to disk or swap if necessary. */
  if (spte->loc == SWAP ||
      spte->loc == STACK ||
      (spte->loc == ZERO && pagedir_is_dirty (t->pagedir, spte->page_uaddr)))
    {
      size_t swap_idx = swap_write_page (f->page_kaddr);
      spte->swap_idx = swap_idx;
      spte->loc = SWAP;
    }
  else if (spte->loc == DISK && pagedir_is_dirty (t->pagedir, spte->page_uaddr))
    {
      lock_acquire (&filesys_lock);
      file_write_at (spte->file, f->page_kaddr, spte->page_bytes, spte->ofs);
      lock_release (&filesys_lock);
    }
  
  /* Remove page mapping from owning thread to complete the eviction. */
  pagedir_clear_page (t->pagedir, spte->page_uaddr);
  f->thread = NULL;
  f->spte = NULL;
  lock_release (&f->lock);
  spte->loaded = false;

  return f->page_kaddr;
}