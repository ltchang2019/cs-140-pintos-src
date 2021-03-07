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
#include "threads/thread.h"

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

  thread_current ()->cwd_inode = inode_open (ROOT_DIR_SECTOR);
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
  //printf ("EXTRACTING NAME AND BASE\n");
  /* Extract base and file/directory name. */
  char *base = extract_base (path);
  char *name = extract_name (path);

  // printf ("Name: %s\n", name);
  // printf ("Base: %s\n", base);

  // printf ("GETTING PARENT SUBDIR INODE\n");
  /* If name == NULL, no slashes so use cwd. Else, open last 
     subdirectory of base. */
  struct inode *parent_inode;
  if (base == NULL)
    parent_inode = inode_reopen (thread_current ()->cwd_inode);
  else
    parent_inode = path_to_inode (base);
    
  // printf ("ACQUIRING LOCK ON PARENT INODE\n");
  // printf ("PARENT INODE ADDR: %p\n", parent_inode);
  /* Acquire lock on directory's in-memory inode. If directory
     already removed, release and return false. */
  lock_acquire (&parent_inode->lock);
  
  if (parent_inode->removed)
    {
      // printf ("WAS REMOVED\n");
      lock_release (&parent_inode->lock);
      return false;
    }

  // printf ("CREATING NEW INODE DISK\n");
  /* Create new inode_disk and add dir_entry. */
  block_sector_t new_inode_sector = 0;
  struct dir *parent_dir = dir_open (parent_inode);
  bool success = (parent_dir != NULL
                  && free_map_allocate (1, &new_inode_sector)
                  && inode_create (new_inode_sector, initial_size, type)
                  && dir_add (parent_dir, name, new_inode_sector));

  // printf ("CREATING . AND ..\n");
  /* If creating directory, add current and parent dir_entries. */
  if (success && type == DIR)
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

  lock_release (&parent_inode->lock);
  dir_close (parent_dir);

  // printf ("RETURNING WITH: %d\n", success);
  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *path)
{
  if (strlen (path) == 0)
    return NULL;

  struct inode *inode = path_to_inode ((char *) path);
  if (inode == NULL)
    return NULL;

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *path) 
{
  char *base = extract_base (path);
  char *name = extract_name (path);

  struct inode *to_remove_inode = path_to_inode (path);

  /* Return false if it is empty directory. */
  if (to_remove_inode == NULL)
    return false;
  
  /* Acquire lock on inode and release once inode->removed
     has been set to true in dir_remove(). */
  lock_acquire (&to_remove_inode->lock);

  /* Return false it is directory that isn't empty or if
     _other_ processes currently have directory open (including
     open for cwd). */
  if (to_remove_inode->type == DIR)
    {
      if (to_remove_inode-> open_cnt > 1)
        {
          lock_release (&to_remove_inode->lock);
          return false;
        }

      struct dir *dir = dir_open (inode_reopen (to_remove_inode));
      bool empty = dir_is_empty (dir);
      dir_close (dir);
      if (!empty)
        {
          lock_release (&to_remove_inode->lock);
          return false;
        }
    }
  
  /* If name == NULL, no slashes so use cwd. Else, open last 
     subdirectory of base. */
  struct inode *parent_inode;
  if (base == NULL)
    parent_inode = inode_reopen (thread_current ()->cwd_inode);
  else
    parent_inode = path_to_inode (base);
    
  /* Note that parent inode could not have been removed since
     we already know there is a child directory. */
  lock_acquire (&parent_inode->lock);
  struct dir *parent_dir = dir_open (parent_inode);
  bool success = parent_dir != NULL && dir_remove (parent_dir, name);
  lock_release (&parent_inode->lock);
  
  dir_close (parent_dir); 
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
