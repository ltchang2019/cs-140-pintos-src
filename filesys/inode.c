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
    off_t length;                           /* File size in bytes. */
    unsigned magic;                         /* Magic number. */
    block_sector_t sectors[INODE_SECTORS];  /* Disk locations of data. */
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

    // NOT NECESSARY FOR THE BUFFER CACHE BUT MIGHT BE IMPORTANT FOR
    // EXTENSIBLE FILES AND SUBDIRECTORIES
    // struct cache_entry *inode_entry;    /* Reference to inode_disk. */
    // struct list data_entries;           /* List of data blocks. */
  };

/* Returns the disk sector that contains byte offset POS within
   INODE. Returns -1 if INODE does not contain data for a byte
   at offset POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);

  /* Get inode_disk block with sector number INODE->SECTOR from
     cache, in order to read length and sectors array. */
  size_t cache_idx = cache_get_block (inode->sector, INODE);
  struct cache_entry *ce = cache_idx_to_cache_entry (cache_idx);
  void *cache_slot = cache_idx_to_cache_slot (cache_idx);
  struct inode_disk *inode_data = (struct inode_disk *) cache_slot;

  /* No data since byte offset POS is past end of file. */
  if (pos >= inode_data->length)
    {
      rw_lock_shared_release (&ce->rw_lock);
      return -1;
    }

  off_t pos_block_num = pos / BLOCK_SECTOR_SIZE;

  /* Find correct disk sector containing data for a byte at POS. */
  if (pos_block_num < NUM_DIRECT)
    {
      block_sector_t sector = inode_data->sectors[pos_block_num];
      rw_lock_shared_release (&ce->rw_lock);
      return sector;
    }
  else if (pos_block_num < NUM_DIR_INDIR)
    {
      /* Get indirect block from cache, since POS refers to a byte
         beyond the data that the direct block (inode_disk) can
         point to. */
      block_sector_t i_sector = inode_data->sectors[INDIR];
      rw_lock_shared_release (&ce->rw_lock);

      size_t i_cache_idx = cache_get_block (i_sector, DATA);
      struct cache_entry *i_ce = cache_idx_to_cache_entry (i_cache_idx);
      void *i_cache_slot = cache_idx_to_cache_slot (i_cache_idx);
      struct indir_block *i_block = (struct indir_block *) i_cache_slot;

      /* Get correct sector number from the indirect block. */
      off_t indir_idx = pos_block_num - NUM_DIRECT;
      block_sector_t sector = i_block->sectors[indir_idx];
      rw_lock_shared_release (&i_ce->rw_lock);
      return sector;
    }
  else if (pos_block_num < NUM_FILE_MAX)
    {
      /* Get doubly indirect block from cache, since POS refers to
         a byte beyond the data that the direct block and indirect
         block can point to. */
      block_sector_t di_sector = inode_data->sectors[DOUBLE_INDIR];
      rw_lock_shared_release (&ce->rw_lock);

      size_t di_cache_idx = cache_get_block (di_sector, DATA);
      struct cache_entry *di_ce = cache_idx_to_cache_entry (di_cache_idx);
      void *di_cache_slot = cache_idx_to_cache_slot (di_cache_idx);
      struct indir_block *di_block = (struct indir_block *) di_cache_slot;

      /* Get sector number of correct indirect block from the doubly
         indirect block. */
      off_t doubly_indir_idx = (pos_block_num - NUM_DIR_INDIR) / NUM_INDIRECT;
      block_sector_t i_sector = di_block->sectors[doubly_indir_idx];
      rw_lock_shared_release (&di_ce->rw_lock);

      /* Get correct indirect block from cache. */
      size_t i_cache_idx = cache_get_block (i_sector, DATA);
      struct cache_entry *i_ce = cache_idx_to_cache_entry (i_cache_idx);
      void *i_cache_slot = cache_idx_to_cache_slot (i_cache_idx);
      struct indir_block *i_block = (struct indir_block *) i_cache_slot;

      /* Get correct sector number from the indirect block. */
      off_t indir_idx = (pos_block_num - NUM_DIR_INDIR) % NUM_INDIRECT;
      block_sector_t sector = i_block->sectors[indir_idx];
      rw_lock_shared_release (&i_ce->rw_lock);

      return sector;
    }
  else
    {
      rw_lock_shared_release (&ce->rw_lock);
      return -1;
    }
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

static void free_disk_block (block_sector_t sector, size_t cache_idx);
static bool add_new_block (struct inode_disk *i_data, block_sector_t sector);

/* Free the cache slot corresponding to CACHE_IDX and free
   the disk sector corresponding to SECTOR. */
static void
free_disk_block (block_sector_t sector, size_t cache_idx)
{
  ASSERT (sector != SECTOR_NOT_PRESENT);

  cache_free_slot (sector, cache_idx);
  free_map_release (sector, 1);
}

/* Adds the sector number SECTOR of a newly allocated disk
   block to the disk_inode I_DATA of a file. If necessary,
   allocate an indirect block or a doubly indirect block
   to contain the entry for the new sector. Return true
   if operation is successful and false otherwise. */
static bool
add_new_block (struct inode_disk *i_data, block_sector_t sector)
{
  bool success = false;
  off_t length = i_data->length;
  off_t length_block_num = length / BLOCK_SECTOR_SIZE;

  if (length == 0)
    {
      /* This is the first data block of the file. */
      i_data->sectors[0] = sector;
      success = true;
    }
  else if (length_block_num < NUM_DIRECT - 1)
    {
      /* Put new sector entry in direct block (disk_inode). */
      i_data->sectors[length_block_num + 1] = sector;
      success = true;
    }
  else if (length_block_num < NUM_DIR_INDIR - 1)
    {
      /* Need to allocate indirect block. */
      if (length_block_num == NUM_DIRECT - 1)
        {
          block_sector_t i_sector = 0;
          if (!free_map_allocate (1, &i_sector))
            return success;

          i_data->sectors[INDIR] = i_sector;
        }

      /* Get indirect block from cache. */
      block_sector_t i_sector = i_data->sectors[INDIR];
      size_t cache_idx = cache_get_block (i_sector, DATA);
      struct cache_entry *ce = cache_idx_to_cache_entry (cache_idx);
      void *cache_slot = cache_idx_to_cache_slot (cache_idx);
      struct indir_block *i_block = (struct indir_block *) cache_slot;
      rw_lock_shared_to_exclusive (&ce->rw_lock);
      ce->dirty = true;

      /* Set all entries to SECTOR_NOT_PRESENT if indirect
         block is newly allocated. */
      if (length_block_num == NUM_DIRECT - 1)
        {
          for (size_t idx = 0; idx < NUM_INDIRECT; idx++)
            i_block->sectors[idx] = SECTOR_NOT_PRESENT;
        }

      /* Add new entry to indirect block. */
      i_block->sectors[length_block_num - NUM_DIRECT + 1] = sector;
      rw_lock_exclusive_release (&ce->rw_lock);
      success = true;
    }
  else if (length_block_num < NUM_FILE_MAX - 1)
    {
      off_t block_num_indir = length_block_num - NUM_DIR_INDIR + 1;
      off_t block_num_doubly_indir = block_num_indir / NUM_INDIRECT;

      /* Need to allocate doubly indirect block. */
      if (block_num_indir == 0)
        {
          block_sector_t di_sector = 0;
          if (!free_map_allocate (1, &di_sector))
            return success;

          i_data->sectors[DOUBLE_INDIR] = di_sector;
        }

      /* Get doubly indirect block from cache. */
      block_sector_t di_sector = i_data->sectors[DOUBLE_INDIR];
      size_t di_cache_idx = cache_get_block (di_sector, DATA);
      struct cache_entry *di_ce = cache_idx_to_cache_entry (di_cache_idx);
      void *di_cache_slot = cache_idx_to_cache_slot (di_cache_idx);
      struct indir_block *di_block = (struct indir_block *) di_cache_slot;
      rw_lock_shared_to_exclusive (&di_ce->rw_lock);
      di_ce->dirty = true;

      /* Set all entries to SECTOR_NOT_PRESENT if doubly
         indirect block is newly allocated. */
      if (block_num_indir == 0)
        {
          for (size_t idx = 0; idx < NUM_INDIRECT; idx++)
            di_block->sectors[idx] = SECTOR_NOT_PRESENT;
        }

      /* Need to allocate indirect block in doubly indirect block. */
      if (block_num_indir % NUM_INDIRECT == 0)
        {
          block_sector_t i_sector = 0;
          if (!free_map_allocate (1, &i_sector))
            {
              rw_lock_exclusive_release (&di_ce->rw_lock);
              return success;
            }

          di_block->sectors[block_num_doubly_indir] = i_sector;
        }

      /* Get indirect block from cache. */
      block_sector_t i_sector = di_block->sectors[block_num_doubly_indir];
      rw_lock_exclusive_release (&di_ce->rw_lock);

      size_t i_cache_idx = cache_get_block (i_sector, DATA);
      struct cache_entry *i_ce = cache_idx_to_cache_entry (i_cache_idx);
      void *i_cache_slot = cache_idx_to_cache_slot (i_cache_idx);
      struct indir_block *i_block = (struct indir_block *) i_cache_slot;
      rw_lock_shared_to_exclusive (&i_ce->rw_lock);
      i_ce->dirty = true;

      /* Set all entries to SECTOR_NOT_PRESENT if indirect
         block is newly allocated. */
      if (block_num_indir % NUM_INDIRECT == 0)
        {
          for (size_t idx = 0; idx < NUM_INDIRECT; idx++)
            i_block->sectors[idx] = SECTOR_NOT_PRESENT;
        }

      /* Add new entry to indirect block in doubly indirect block. */
      i_block->sectors[block_num_indir % NUM_INDIRECT] = sector;
      rw_lock_exclusive_release (&i_ce->rw_lock);

      success = true;
    }

  return success;
}

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with zero bytes of data and writes
   the new inode to sector SECTOR on the file system
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
      disk_inode->length = 0;
      disk_inode->magic = INODE_MAGIC;
      for (size_t idx = 0; idx < INODE_SECTORS; idx++)
        disk_inode->sectors[idx] = SECTOR_NOT_PRESENT;

      /* Put new disk_inode on disk. */
      block_write (fs_device, sector, disk_inode);
      free (disk_inode);
      success = true;
    }
  
  return success;
}

/* Reads an inode from SECTOR. Returns a `struct inode'
   that has a reference to SECTOR. Returns a null pointer
   if memory allocation fails for the `struct inode'. */
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
          /* Get inode_disk block from cache. */
          size_t cache_idx = cache_get_block (inode->sector, INODE);
          void *cache_slot = cache_idx_to_cache_slot (cache_idx);
          struct inode_disk *inode_data = (struct inode_disk *) cache_slot;

          /* Free data blocks pointed to by direct block (disk_inode). */
          for (size_t idx = 0; idx < NUM_DIRECT; idx++)
            {
              block_sector_t sector = inode_data->sectors[idx];
              if (sector == SECTOR_NOT_PRESENT)
                break;

              free_disk_block (sector, CACHE_IDX_SEARCH);
            }

          /* Free data blocks pointed to by indirect block. */
          if (inode_data->sectors[INDIR] != SECTOR_NOT_PRESENT)
            {
              /* Get indirect block from cache. */
              block_sector_t i_sector = inode_data->sectors[INDIR];
              size_t cache_idx = cache_get_block (i_sector, DATA);
              void *cache_slot = cache_idx_to_cache_slot (cache_idx);
              struct indir_block *i_block = (struct indir_block *) cache_slot;

              /* Free data blocks pointed to by indirect block. */
              for (size_t idx = 0; idx < NUM_INDIRECT; idx++)
                {
                  block_sector_t sector = i_block->sectors[idx];
                  if (sector == SECTOR_NOT_PRESENT)
                    break;

                  free_disk_block (sector, CACHE_IDX_SEARCH);
                }
              
              /* Free the indirect block. */
              free_disk_block (i_sector, cache_idx);
            }
         
          /* Free data blocks pointed to by doubly indirect block. */
          if (inode_data->sectors[DOUBLE_INDIR] != SECTOR_NOT_PRESENT)
            {
              /* Get doubly indirect block from cache. */
              block_sector_t di_sector = inode_data->sectors[DOUBLE_INDIR];
              size_t cache_idx = cache_get_block (di_sector, DATA);
              void *slot = cache_idx_to_cache_slot (cache_idx);
              struct indir_block *di_block = (struct indir_block *) slot;

              /* Iterate through indirect blocks of doubly indirect block. */
              for (size_t d_idx = 0; d_idx < NUM_INDIRECT; d_idx++)
                {
                  if (di_block->sectors[d_idx] == SECTOR_NOT_PRESENT)
                    break;

                  /* Get indirect block from cache. */
                  block_sector_t i_sector = di_block->sectors[d_idx];
                  size_t cache_idx = cache_get_block (i_sector, DATA);
                  void *slot = cache_idx_to_cache_slot (cache_idx);
                  struct indir_block *i_block = (struct indir_block *) slot;

                  /* Free data blocks pointed to by indirect block. */
                  for (size_t idx = 0; idx < NUM_INDIRECT; idx++)
                    {
                      block_sector_t sector = i_block->sectors[idx];
                      if (sector == SECTOR_NOT_PRESENT)
                        break;

                      free_disk_block (sector, CACHE_IDX_SEARCH);
                    }
                  
                  /* Free the indirect block. */
                  free_disk_block (i_sector, cache_idx);
                }
            
              /* Free the doubly indirect block. */
              free_disk_block (di_sector, cache_idx);
            }

          /* Free the direct block (disk_inode). */
          free_disk_block (inode->sector, cache_idx);
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

      /* Offset is past end of file, so return zero bytes read. */
      if (sector_idx == SECTOR_NOT_PRESENT)
        return 0;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk = size < min_left ? size : min_left;

      /* Get data block from cache. */
      size_t cache_idx = cache_get_block (sector_idx, DATA);
      struct cache_entry *ce = cache_idx_to_cache_entry (cache_idx);
      void *cache_slot = cache_idx_to_cache_slot (cache_idx);

      if (sector_ofs == 0 && chunk == BLOCK_SECTOR_SIZE)
        {
          /* Copy full block from cache into caller's buffer. */
          memcpy (buffer + bytes_read, cache_slot, BLOCK_SECTOR_SIZE);
        }
      else
        {
          /* Partially copy block from cache into caller's buffer. */
          memcpy (buffer + bytes_read, cache_slot + sector_ofs, chunk);
        }
      rw_lock_shared_release (&ce->rw_lock);
      
      /* Advance. */
      size -= chunk;
      offset += chunk;
      bytes_read += chunk;

      /* Read-ahead and load the next data block into the cache
         asynchronously if there is a next data block. */
      if (size > 0)
        {
          block_sector_t next_sector = byte_to_sector (inode, offset);
          if (next_sector != SECTOR_NOT_PRESENT)
            read_ahead_signal (next_sector);
        }
    }

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes written, which may be less than
   SIZE if an error occurs.

   Writes that go beyond the end of file extend the file, up to
   the maximum allowable file size. If OFFSET is beyond end of
   file to begin with, the file is first extended to OFFSET and
   the gap in between is filled with zeros. */
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

      if (sector_idx == SECTOR_NOT_PRESENT)
        {
          /* Allocate a new disk sector. */
          block_sector_t new_sector = 0;
          if (!free_map_allocate (1, &new_sector))
            return bytes_written;

          /* Get inode_disk block of file from cache. */
          size_t i_cache_idx = cache_get_block (inode->sector, INODE);
          struct cache_entry *i_ce = cache_idx_to_cache_entry (i_cache_idx);
          void *i_cache_slot = cache_idx_to_cache_slot (i_cache_idx);
          struct inode_disk *inode_data = (struct inode_disk *) i_cache_slot;
          rw_lock_shared_to_exclusive (&i_ce->rw_lock);
          i_ce->dirty = true;

          /* Write new sector number to disk_inode. */
          if (!add_new_block (inode_data, new_sector))
            {
              rw_lock_exclusive_release (&i_ce->rw_lock);
              free_map_release (new_sector, 1);
              return bytes_written;
            }

          /* Update length of file. */
          int chunk = size < BLOCK_SECTOR_SIZE ? size : BLOCK_SECTOR_SIZE;
          inode_data->length += chunk;
          rw_lock_exclusive_release (&i_ce->rw_lock);
          
          /* Get new sector into the cache. */
          size_t d_cache_idx = cache_get_block (new_sector, DATA);
          struct cache_entry *d_ce = cache_idx_to_cache_entry (d_cache_idx);
          void *d_cache_slot = cache_idx_to_cache_slot (d_cache_idx);
          rw_lock_shared_to_exclusive (&d_ce->rw_lock);
          d_ce->dirty = true;

          /* Write data to the new sector. */
          memcpy (d_cache_slot, buffer + bytes_written, chunk);
          rw_lock_exclusive_release (&d_ce->rw_lock);

          /* Advance and update length of file. */
          size -= chunk;
          offset += chunk;
          bytes_written += chunk;
        }
      else
        {
          /* Bytes left in inode, bytes left in sector, lesser of the two. */
          off_t inode_left = inode_length (inode) - offset;
          int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
          int min_left = inode_left < sector_left ? inode_left : sector_left;

          /* Number of bytes to actually write into this sector. */
          int chunk = size < min_left ? size : min_left;

          /* Get data block from cache. */
          size_t cache_idx = cache_get_block (sector_idx, DATA);
          struct cache_entry *ce = cache_idx_to_cache_entry (cache_idx);
          void *cache_slot = cache_idx_to_cache_slot (cache_idx);
          ce->dirty = true;

          if (sector_ofs == 0 && chunk == BLOCK_SECTOR_SIZE)
            {
              /* Write full block from user buffer to cache slot. */
              memcpy (cache_slot, buffer + bytes_written, BLOCK_SECTOR_SIZE);
            }
          else 
            {
              /* If the block does not contain data before or after the 
                chunk we're writing, zero out the block before writing. */
              if (sector_ofs == 0 && chunk >= sector_left)
                memset (cache_slot, 0, BLOCK_SECTOR_SIZE);
              memcpy (cache_slot + sector_ofs, buffer + bytes_written, chunk);
            }
          rw_lock_shared_release (&ce->rw_lock);

          /* Advance. */
          size -= chunk;
          offset += chunk;
          bytes_written += chunk;
        }
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
  /* Get inode_disk from cache in order to read length field. */
  size_t cache_idx = cache_get_block (inode->sector, INODE);
  struct cache_entry *ce = cache_idx_to_cache_entry (cache_idx);
  void *cache_slot = cache_idx_to_cache_slot (cache_idx);
  struct inode_disk *inode_data = (struct inode_disk *) cache_slot;

  off_t length = inode_data->length;
  rw_lock_shared_release (&ce->rw_lock);

  return length;
}
