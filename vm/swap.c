#include "vm/swap.h"
#include <bitmap.h>
#include "devices/block.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

/* Number of sectors that fit into a page. */
#define SECTORS_PER_PG (PGSIZE / BLOCK_SECTOR_SIZE)

/* Swap table. */
struct swap
  {
    struct lock lock;         /* Mutual exclusion. */
    struct bitmap *used_map;  /* Bitmap of free swap slots. */
    struct block *block;      /* Reference to swap block. */
  };

/* Reference to the swap table. */
static struct swap *swap;

/* Initializes the swap table. */
void
swap_table_init (void)
{
  swap = malloc (sizeof (struct swap));
  if (swap == NULL)
    PANIC ("swap_init: failed to allocate swap table");

  struct block *swap_block = block_get_role (BLOCK_SWAP);
  block_sector_t swap_size = block_size (swap_block) / SECTORS_PER_PG;
  lock_init (&swap->lock);
  swap->used_map = bitmap_create (swap_size);
  swap->block = swap_block;
}

/* Write a page of memory with kernel virtual address KPAGE
   to swap. Return index of swap slot to which page was
   written. Panic the kernel if the swap partition is full. */
size_t
swap_write_page (const void *kpage)
{
  lock_acquire (&swap->lock);
  size_t swap_idx = bitmap_scan_and_flip (swap->used_map, 0, 1, false);
  lock_release (&swap->lock);

  if (swap_idx == BITMAP_ERROR)
    PANIC ("swap_get_slot: out of swap slots");
  
  /* Write page in BLOCK_SECTOR_SIZE chunks to the swap slot. */
  block_sector_t sector = swap_idx * SECTORS_PER_PG;
  uint8_t *ofs = (void *) kpage;
  for (size_t idx = 0; idx < SECTORS_PER_PG; idx++)
    block_write (swap->block, sector + idx, ofs + idx * BLOCK_SECTOR_SIZE);

  return swap_idx;
}

/* Read a page of data in the swap slot indexed by SWAP_IDX
   into the page of memory with kernel virtual address KPAGE. */
void
swap_read_page (void *kpage, size_t swap_idx)
{
  block_sector_t sector = swap_idx * SECTORS_PER_PG;
  uint8_t *ofs = (void *) kpage;
  for (size_t idx = 0; idx < SECTORS_PER_PG; idx++)
    block_read (swap->block, sector + idx, ofs + idx * BLOCK_SECTOR_SIZE);

  /* Set bit at SWAP_IDX to 0 to indicate the swap slot is now free. */
  swap_free_slot (swap_idx);
}

/* Frees a swap slot by setting it's index bit in the bitmap to 0. */
void
swap_free_slot (size_t swap_idx)
{
  ASSERT (bitmap_test (swap->used_map, swap_idx));

  lock_acquire (&swap->lock);
  bitmap_set (swap->used_map, swap_idx, false);
  lock_release (&swap->lock);
}
