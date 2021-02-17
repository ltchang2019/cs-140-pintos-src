#include <debug.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "vm/frame.h"

static void *user_pool_base;
static struct frame_entry *frame_table_base;

static struct frame_entry *page_kaddr_to_frame_addr (void *page_kaddr);

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
  size_t free_frames = (free_end - free_start) / (PGSIZE * 2);
  if (free_frames > num_frames)
    free_frames = num_frames;

  /* Initialize frames in frame table. */
  frame_table_base = malloc (sizeof (struct frame_entry) * free_frames);
  for (size_t index = 0; index < free_frames; index++)
    {
      struct frame_entry *f = frame_table_base + index;
      f->page_kaddr = NULL;
      f->spte = NULL;
      lock_init (&f->lock);
    }

  /* Get base address of the user pool. */
  user_pool_base = palloc_get_user_pool_base ();
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
    return NULL;

  /* Get index to available frame and set fields in frame. */
  struct frame_entry *f = page_kaddr_to_frame_addr (page_kaddr);
  f->page_kaddr = page_kaddr;
  f->spte = spte;

  /* Load data into the page depending on it's location. */
  if (spte->loc == ZERO)
      memset (page_kaddr, 0, PGSIZE);
  else if (spte->loc == DISK)
    {
      /* Read the non-zero bytes of the page and zero the rest. */
      size_t num_bytes = spte->page_bytes;
      lock_acquire (&filesys_lock);
      off_t bytes_read = file_read_at (spte->file, page_kaddr,
                                       num_bytes, spte->ofs);
      lock_release (&filesys_lock);
      
      if (bytes_read != (int) num_bytes)
        {
          palloc_free_page (page_kaddr);
          return NULL;
        }
      memset (page_kaddr + num_bytes, 0, PGSIZE - num_bytes);
    }

  return page_kaddr;
}

/* Remove a page with kernel virtual address PAGE_KADDR from
   it's frame and free the page. */
void
frame_free_page (void *page_kaddr)
{
  struct frame_entry *f = page_kaddr_to_frame_addr (page_kaddr);
  f->page_kaddr = NULL;
  f->spte = NULL;

  palloc_free_page (page_kaddr);
}