#include "vm/frame.h"
#include <debug.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "vm/swap.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"

static void *user_pool_base;
static struct frame_entry *frame_table_base;
static struct frame_entry *lead_hand;
static struct frame_entry *lag_hand;
static size_t free_frames;

static size_t debug_counter = 0;

static struct frame_entry *page_kaddr_to_frame_addr (void *page_kaddr);
static void *frame_evict_page (void);

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

  for (size_t index = 0; index < free_frames; index++)
    {
      struct frame_entry *f = frame_table_base + index;
      f->page_kaddr = NULL;
      f->spte = NULL;
      f->thread = NULL;
      lock_init (&f->lock);
    }

  /* Get base address of the user pool. */
  user_pool_base = palloc_get_user_pool_base ();

  /* Initialize leading and lagging hands. */
  lead_hand = frame_table_base;
  lag_hand = frame_table_base + 1;
}

/* Obtain a page from the user pool and store in the next
   available frame. Depending on the fields in the supplemental
   page table entry for the page, fill the page with the
   appropriate data. Return NULL if unsuccessful. */
void *
frame_alloc_page (enum palloc_flags flags, struct spte *spte)
{
  ASSERT (flags & PAL_USER);

  /* Get a page of memory. */
  void *page_kaddr = palloc_get_page (flags);
  if (page_kaddr == NULL)
    {
      page_kaddr = frame_evict_page ();
      ASSERT (page_kaddr != NULL);
    }

  /* Get index to available frame and set fields in frame. */
  struct frame_entry *f = page_kaddr_to_frame_addr (page_kaddr);

  lock_acquire (&f->lock);
  f->page_kaddr = page_kaddr;
  f->spte = spte;
  f->thread = thread_current ();

  /* Load data into the page depending on it's location. */
  if (spte->loc == ZERO || spte->loc == STACK)
    {
      memset (page_kaddr, 0, PGSIZE);
      //printf ("%zu\n", debug_counter++);
    }
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
  f->spte = NULL;
  f->thread = NULL;

  palloc_free_page (page_kaddr);
  lock_release (&f->lock);
}

/* Evict a page from it's frame and return the kernel virtual
   address of the frame that is now free to be used. */
static void *
frame_evict_page (void)
{
  /* Access dirty and access bits through user virtual address. */
  /* Choose random page for now. */
  /* Old spte, new spte. */
  /* pagedir_clear_page, pagedir_set_page. */
  struct frame_entry *f = lead_hand;
  struct thread *t = f->thread;
  lock_acquire (&f->lock);
  
  lead_hand = lead_hand + 10;
  if (lead_hand >= frame_table_base + free_frames)
    lead_hand = frame_table_base;

  struct spte *spte = f->spte;
  void *upage = spte->page_uaddr;

  //if (pagedir_is_dirty (t->pagedir, upage) && spte->loc == SWAP)
  if (spte->loc == SWAP)
    {
      size_t swap_idx = swap_write_page (f->page_kaddr);
      spte->swap_idx = swap_idx;
    }
  else if (pagedir_is_dirty (t->pagedir, upage) && spte->loc == ZERO)
    {
      size_t swap_idx = swap_write_page (f->page_kaddr);
      spte->swap_idx = swap_idx;
      spte->loc = SWAP;
    }
  else if (spte->loc == STACK)
    {
      size_t swap_idx = swap_write_page (f->page_kaddr);
      spte->swap_idx = swap_idx;
      spte->loc = SWAP;
    }
  else if (pagedir_is_dirty (t->pagedir, upage) && spte->loc == DISK)
    {
      lock_acquire (&filesys_lock);
      file_write_at (spte->file, f->page_kaddr, spte->page_bytes, spte->ofs);
      lock_release (&filesys_lock);
    }

  pagedir_clear_page (t->pagedir, upage);
  spte->loaded = false;
  lock_release (&f->lock);
  return f->page_kaddr;
}