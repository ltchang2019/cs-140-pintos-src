#include "filesys/path.h"

/* 
  /a/b/c
  ./a/b/c
  c
 */
struct inode * 
path_to_inode (char *path)
{
  char *token, *ptr;

  struct dir *dir = get_start_dir (path);

  // Append trailing slash to smooth out strtok_r
  strlcat (path, "/", strlen (path) + 1);

  struct inode *inode = NULL;
  for (token = strtok_r (path, "/", &ptr); token != NULL; 
       token = strtok_r (NULL, "/", &ptr))
    {
      const struct dir *const_dir = (const struct dir *) dir;
      const char *const_name = (const char *) token;
      
      // TODO: read lock before search? Or taken care of in inode_read_at?
      if (dir_lookup (const_dir, const_name, &inode))
        {
          inode_close (dir->inode);
          dir->inode = inode;
          dir->pos = 0;
        }
      else
        return NULL;
    }
  
  return inode;
}

struct dir *
get_start_dir (char *path)
{
  struct dir *dir;
  if (path[0] == '/')
    {
      path = path + 1;
      dir = dir_open_root ();
    }
  else
    {
      // TODO: add get_cwd
    }

  return dir;
}