#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"

/* The total number of sectors (indir_block or data) that
   can be pointed to by an inode_disk struct. */
#define INODE_SECTORS 126

/* The number of data sectors that can be pointed to
   directly by an inode_disk struct. */
#define NUM_DIRECT 124

/* The number of data sectors that can be pointed to
   by an indir_block struct. */
#define NUM_INDIRECT 128

/* Constants needed for byte to sector calculations. */
#define INDIR NUM_DIRECT
#define DOUBLE_INDIR (NUM_DIRECT + 1)
#define NUM_DIR_INDIR (NUM_DIRECT + NUM_INDIRECT)
#define NUM_DOUBLE_INDIR (NUM_INDIRECT * NUM_INDIRECT)
#define NUM_FILE_MAX (NUM_DIR_INDIR + NUM_DOUBLE_INDIR)

/* An indirect block contains sector numbers which
   refer to blocks that contain actual data.
   
   Must be exactly BLOCK_SECTOR_SIZE bytes in size. */
struct indir_block
  {
    block_sector_t sectors[NUM_INDIRECT];
  };

struct bitmap;

void inode_init (void);
bool inode_create (block_sector_t, off_t);
struct inode *inode_open (block_sector_t);
struct inode *inode_reopen (struct inode *);
block_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);

#endif /* filesys/inode.h */
