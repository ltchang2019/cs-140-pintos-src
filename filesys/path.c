#include "filesys/path.h"
#include "filesys/filesys.h"

/* Primarily called to open last parent subdirectory in a full 
   path. Converts string PATH to an open inode, which the caller 
   is responsible for closing. */
struct inode * 
path_to_inode (char *path)
{
  char *token, *ptr;

  struct dir *dir = get_start_dir (path);

  /* Append trailing slash to smooth out strtok_r. */
  strlcat (path, "/", strlen (path) + 1);

  struct inode *inode = NULL;
  for (token = strtok_r (path, "/", &ptr); token != NULL; 
       token = strtok_r (NULL, "/", &ptr))
    {
      const struct dir *const_dir = (const struct dir *) dir;
      const char *const_name = (const char *) token;
      
      /* Synchronization in inode_read_at makes dir_lookup safe
         in context of potentially searching for dir_entry being removed. */
      if (dir_lookup (const_dir, const_name, &inode))
        {
          if (dir->inode->sector != ROOT_DIR_SECTOR)
            inode_close (dir->inode);
          dir->inode = inode;
          dir->pos = 0;
        }
      else
        return NULL;
    }
  
  return inode;
}

/* Gets the starting directory for a path to inode conversion. 
   If first char isn't a slash, we use our cwd as starting point
   for conversion. */
struct dir *
get_start_dir (char *path)
{
  struct dir *dir;
  if (path[0] == '/')
      dir = dir_open_root ();
  else
      dir = dir_open_cwd ();

  return dir;
}

/* Given path PATH, replaces last slash with \0 and
   fills END with position of the directory name to
   be added. If the path has no slashes, END is NULL
   and PATH remains the same. */
void
extract_base_and_name (char **path, char **name)
{
  /* Get pointer to last slash. */
  char *last_slash = strrchr (*path, '/');
  if (last_slash == NULL)
    return;
  
  /* Set slash to null terminator and set END to directory 
     name (last token). */
  *last_slash = '\0';
  *name = last_slash + 1;
}