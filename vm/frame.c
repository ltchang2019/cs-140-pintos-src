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
static struct lock get_frame_lock;
static size_t clock_timeout;
static size_t frame_cnt;

static struct frame_entry *page_kaddr_to_frame_addr (void *page_kaddr);
static struct frame_entry *frame_evict_page (void);
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
frame_table_init (void)
{
  /* Number of available frames is equal to the number
     of available user pages. */
  frame_cnt = palloc_get_num_user_pages ();

  /* Initialize frames in frame table. */
  frame_table_base = malloc (sizeof (struct frame_entry) * frame_cnt);
  if (frame_table_base == NULL)
    PANIC ("Not enough memory for frame table.");

  for (size_t idx = 0; idx < frame_cnt; idx++)
    {
      struct frame_entry *f = frame_table_base + idx;
      f->page_kaddr = NULL;
      f->spte = NULL;
      f->thread = NULL;
      lock_init (&f->lock);
    }

  /* Initialize lock on clock algorithm usage and clock timeout. */
  lock_init (&get_frame_lock);
  clock_timeout = 0;

  /* Get base address of the user pool. */
  user_pool_base = palloc_get_user_pool_base ();

  /* Initialize leading and lagging hands for clock algorithm. */
  lag_hand = frame_table_base;
  lead_hand = frame_table_base + (frame_cnt / 4);
}

/* Obtain a page from the user pool and store it in the next
   available frame. Depending on the fields in the supplemental
   page table entry for the page, fill the page with the
   appropriate data. Return NULL if unsuccessful. */
void *
frame_alloc_page (enum palloc_flags flags, struct spte *spte)
{
  ASSERT (flags & PAL_USER);

  struct frame_entry *f;

  /* Get a page of memory. Evict a page if necessary. */
  lock_acquire (&get_frame_lock);
  void *page_kaddr = palloc_get_page (flags);
  if (page_kaddr == NULL)
    {
      f = frame_evict_page ();

      /* In the highly unlikely case that both palloc_get_page 
        and our eviction algorithm were unable to find a page,
        return NULL. */
      if (f == NULL)
        return NULL;

      ASSERT (lock_held_by_current_thread (&f->lock));
    }
  else
    {
      f = page_kaddr_to_frame_addr (page_kaddr);
      lock_acquire (&f->lock);
      f->page_kaddr = page_kaddr;
    }
  lock_release (&get_frame_lock);

  f->spte = spte;
  f->thread = thread_current ();

  /* Load data into the page depending on it's location/type. */
  if (spte->loc == ZERO || spte->loc == STACK)
    memset (f->page_kaddr, 0, PGSIZE);
  else if (spte->loc == SWAP && !spte->loaded)
    {
      swap_read_page (f->page_kaddr, spte->swap_idx);
      spte->swap_idx = SWAP_DEFAULT;
    }
  else if ((spte->loc == DISK || spte->loc == MMAP) && !spte->loaded)
    {
      /* Read the non-zero bytes of the page from the file on disk
         and zero the remaining bytes. */
      lock_acquire (&filesys_lock);
      off_t bytes_read = file_read_at (spte->file, f->page_kaddr,
                                       spte->page_bytes, spte->ofs);
      lock_release (&filesys_lock);

      /* If file read error, free page and return NULL. */
      if (bytes_read != (int) spte->page_bytes)
        {
          palloc_free_page (f->page_kaddr);
          return NULL;
        }
      memset (f->page_kaddr + bytes_read, 0, PGSIZE - bytes_read);
    }

  spte->loaded = true;
  lock_release (&f->lock);

  return f->page_kaddr;
}

/* Remove a page with kernel virtual address PAGE_KADDR from
   it's frame and free the page. NOTE that a process must obtain
   the frame's lock to clear the frame's fields. This ensures that 
   a process cannot set the frame's fields to NULL while another 
   process is reading that frame's data. */
void
frame_free_page (void *page_kaddr)
{
  struct frame_entry *f = page_kaddr_to_frame_addr (page_kaddr);
  lock_acquire (&f->lock);

  /* If we no longer own frame, return. */
  if (f->thread != thread_current ())
    {
      lock_release (&f->lock);
      return;
    }

  pagedir_clear_page (thread_current ()->pagedir, f->spte->page_uaddr);
  f->page_kaddr = NULL;
  f->spte = NULL;
  f->thread = NULL;
  palloc_free_page (page_kaddr); 
  lock_release (&f->lock);
}

/* Find a frame to evict according to the second chance clock
   algorithm. The lead hand clears the access bit and the lag
   hand evicts a page if it's access bit is 0. NOTE that access
   to the clock algorithm is mutually exclusive. */
static struct frame_entry *
clock_find_frame (void)
{
  ASSERT (lock_held_by_current_thread (&get_frame_lock));

  while (true)
    {
      /* After a full iteration through the frame table, if no
         eviction candidate can be found, return NULL. */
      if (clock_timeout >= frame_cnt)
        return NULL;

      /* Clear access bit of page that lead hand points to. */
      lock_acquire (&lead_hand->lock);
      if (lead_hand->thread != NULL)
        pagedir_set_accessed (lead_hand->thread->pagedir,
                              lead_hand->spte->page_uaddr, false);
      lock_release (&lead_hand->lock);

      /* Get lock on page that is candidate for eviction. */
      if (lock_try_acquire (&lag_hand->lock))
        {
          ASSERT (lock_held_by_current_thread (&lag_hand->lock));

          struct frame_entry *f = NULL;

          /* If page can be evicted or the frame is free, advance clock
             hands, reset the clock timeout, and return the frame. */
          if (lag_hand->thread == NULL)
            f = lag_hand;
          else if (!pagedir_is_accessed (lag_hand->thread->pagedir,    
                                         lag_hand->spte->page_uaddr))
            f = lag_hand;
          
          if (f != NULL)
            {
              clock_advance ();
              clock_timeout = 0;
              return f;
            }

          lock_release (&lag_hand->lock);
        }
      
      /* Eviction candidate is still being accessed, so continue
         searching for page to evict by advancing clock hands. */
      clock_advance ();
      clock_timeout++;
    }
    
    NOT_REACHED ();
}

/* Advance the lead and lag hands for the clock algorithm by
   one frame, wrapping around to the beginning if the end of
   the frame table is reached. */
static void
clock_advance (void)
{
  if (++lead_hand >= frame_table_base + frame_cnt)
    lead_hand = frame_table_base;
  if (++lag_hand >= frame_table_base + frame_cnt)
    lag_hand = frame_table_base;
}

/* Evict a page from it's frame and return the kernel virtual
   address that is now free to be used by another process. */
static struct frame_entry *
frame_evict_page (void)
{
  struct frame_entry *f = clock_find_frame ();
  ASSERT (lock_held_by_current_thread (&f->lock));

  /* If clock algorithm completed a full cycle through the frame table
     and could not find a frame to evict, return NULL. */
  if (f == NULL)
    return NULL;
  
  struct thread *t = f->thread;
  struct spte *spte = f->spte;
  void *upage = spte->page_uaddr;

  /* Write current page in frame to disk or swap if necessary. */
  if (spte->loc == SWAP ||
      spte->loc == STACK ||
      (spte->loc == ZERO && pagedir_is_dirty (t->pagedir, upage)) ||
      (spte->loc == DISK && pagedir_is_dirty (t->pagedir, upage)))
    {
      size_t swap_idx = swap_write_page (f->page_kaddr);
      spte->swap_idx = swap_idx;
      spte->loc = SWAP;
    }
  else if (spte->loc == MMAP && pagedir_is_dirty (t->pagedir, upage))
    {
      lock_acquire (&filesys_lock);
      file_write_at (spte->file, f->page_kaddr, spte->page_bytes, spte->ofs);
      lock_release (&filesys_lock);
    }
  
  /* Remove page mapping from owning thread to complete the eviction. */
  pagedir_clear_page (t->pagedir, spte->page_uaddr);
  f->thread = NULL;
  f->spte = NULL;
  spte->loaded = false;

  return f;
}
