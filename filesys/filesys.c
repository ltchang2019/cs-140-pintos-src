#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/cache.h"
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/path.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  cache_init ();
  inode_init ();
  free_map_init ();

  if (format) 
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  cache_flush ();
  free_map_close ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *path, off_t initial_size, enum inode_type type) 
{
  /* Extract base and file/directory name. */
  char *base = (char *) path;
  char *name = NULL;
  extract_base_and_name (&base, &name);

  /* If name == NULL, no slashes so use cwd. Else, open last 
     subdirectory of base. */
  struct inode *parent_inode;
  if (name == NULL)
    parent_inode = inode_open (0); // CWD
  else
    parent_inode = path_to_inode (base);
  
  /* Acquire lock on directory's in-memory inode. If directory
     already removed, release and return false. */
  lock_acquire (&parent_inode->lock);

  if (parent_inode->removed)
    {
      lock_release (&parent_inode->lock);
      return false;
    }

  /* Atomically create new inode_disk and add dir_entry. Free resources
     if any part fails. */
  block_sector_t new_inode_sector = 0;
  struct dir *parent_dir = dir_open (parent_inode);
  bool success = (parent_dir != NULL
                  && free_map_allocate (1, &new_inode_sector)
                  && inode_create (new_inode_sector, initial_size, type)
                  && dir_add (parent_dir, name, new_inode_sector));

  /* If creating directory, add current and parent dir_entries. */
  if (type == DIR)
    {
      struct dir *new_dir = dir_open (inode_open (new_inode_sector));
      success = (success 
                 && dir_add (new_dir, ".", new_inode_sector)
                 && dir_add (new_dir, "..", parent_inode->sector));
      dir_close (new_dir);
    }
  
  /* If new directory creation at all failed, set allocated disk
     block to free. */
  if (!success && new_inode_sector != 0) 
    free_map_release (new_inode_sector, 1);

  dir_close (parent_dir);
  lock_release (&parent_inode->lock);

  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  struct dir *dir = dir_open_root ();
  struct inode *inode = NULL;

  if (dir != NULL)
    dir_lookup (dir, name, &inode);
  dir_close (dir);

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  struct dir *dir = dir_open_root ();
  bool success = dir != NULL && dir_remove (dir, name);
  dir_close (dir); 

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
