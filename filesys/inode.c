#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <stdio.h>
#include <string.h>
#include "filesys/cache.h"
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    block_sector_t start;               /* First data sector. */
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    uint32_t unused[125];               /* Not used. */
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */

    // struct inode_disk data;             /* Inode content. */
    // DELETE STRUCT INODE_DISK DATA;

    struct cache_entry *inode_entry;    /* Reference to inode_disk in cache. */
    struct list data_entries;           /* List of data blocks in cache. */
  };

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);

  /* Read inode_disk from cache. */
  size_t cache_idx = cache_load (inode->sector);
  struct cache_entry *ce = cache_idx_to_cache_entry (cache_idx);
  void *cache_slot = cache_idx_to_cache_slot (cache_idx);
  struct inode_disk *inode_data = (struct inode_disk *) cache_slot;
  ce->type = INODE;
  ce->accessed = true;
  off_t length = inode_data->length;
  block_sector_t start = inode_data->start;
  rw_lock_shared_release (&ce->rw_lock);

  /* Examine length relative to pos. */
  if (pos < length)
    return start + pos / BLOCK_SECTOR_SIZE;
  else
    return -1;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      size_t sectors = bytes_to_sectors (length);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      if (free_map_allocate (sectors, &disk_inode->start)) 
        {
          block_write (fs_device, sector, disk_inode);
          if (sectors > 0) 
            {
              static char zeros[BLOCK_SECTOR_SIZE];
              size_t i;
              
              for (i = 0; i < sectors; i++) 
                block_write (fs_device, disk_inode->start + i, zeros);
            }
          success = true; 
        } 
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR, loading it from disk into
   the cache if it is not already present. Returns a
   `struct inode' that points to the cache_entry for the
   inode. Returns a null pointer if memory allocation
   fails for the `struct inode'. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  inode->inode_entry = NULL;
  list_init (&inode->data_entries);

  // block_read (fs_device, inode->sector, &inode->data);
  // DELETE SINCE INODE_DISK NO LONGER EMBEDDED IN STRUCT INODE

  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          /* Read inode_disk from cache. */
          size_t cache_idx = cache_load (inode->sector);
          struct cache_entry *ce = cache_idx_to_cache_entry (cache_idx);
          void *cache_slot = cache_idx_to_cache_slot (cache_idx);
          struct inode_disk *inode_data = (struct inode_disk *) cache_slot;
          ce->type = INODE;
          ce->accessed = true;
          off_t length = inode_data->length;
          block_sector_t start = inode_data->start;
          rw_lock_shared_release (&ce->rw_lock);

          free_map_release (inode->sector, 1);
          free_map_release (start, bytes_to_sectors (length)); 
        }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      /* Read data block from cache. */
      size_t cache_idx = cache_load (sector_idx);
      struct cache_entry *ce = cache_idx_to_cache_entry (cache_idx);
      void *cache_slot = cache_idx_to_cache_slot (cache_idx);
      ce->type = DATA;
      ce->accessed = true;
      ce->inode = inode;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Copy full block from cache into caller's buffer. */
          memcpy (buffer + bytes_read, cache_slot, BLOCK_SECTOR_SIZE);
        }
      else
        {
          /* Partially copy block from cache into caller's buffer. */
          memcpy (buffer + bytes_read, cache_slot + sector_ofs, chunk_size);
        }

      /* Release rw_lock on the cache_entry for this block. */
      rw_lock_shared_release (&ce->rw_lock);
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;

  if (inode->deny_write_cnt)
    return 0;

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      /* Read data block from cache. */
      size_t cache_idx = cache_load (sector_idx);
      struct cache_entry *ce = cache_idx_to_cache_entry (cache_idx);
      void *cache_slot = cache_idx_to_cache_slot (cache_idx);
      ce->type = DATA;
      ce->dirty = true;
      ce->accessed = true;
      ce->inode = inode;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full block from user buffer to cache slot. */
          memcpy (cache_slot, buffer + bytes_written, BLOCK_SECTOR_SIZE);
        }
      else 
        {
          /* If the block does not contain data before or after the 
             chunk we're writing, we first zero out the block. */
          if (sector_ofs == 0 && chunk_size >= sector_left)
            memset (cache_slot, 0, BLOCK_SECTOR_SIZE);
          memcpy (cache_slot + sector_ofs, buffer + bytes_written, chunk_size);
        }

      /* Release rw_lock on the cache_entry for this block. */
      rw_lock_shared_release (&ce->rw_lock);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  /* Read inode_disk from cache. */
  size_t cache_idx = cache_load (inode->sector);
  struct cache_entry *ce = cache_idx_to_cache_entry (cache_idx);
  void *cache_slot = cache_idx_to_cache_slot (cache_idx);
  struct inode_disk *inode_data = (struct inode_disk *) cache_slot;
  ce->type = INODE;
  ce->accessed = true;
  off_t length = inode_data->length;
  rw_lock_shared_release (&ce->rw_lock);

  return length;
}
