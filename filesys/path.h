#ifndef FILESYS_PATH_H
#define FILESYS_PATH_H

#include <string.h>
#include "filesys/directory.h"
#include "filesys/inode.h"

struct inode *path_to_inode (const char *path);
struct dir *get_start_dir (const char *path);
char *extract_base (const char *path);
char *extract_name (const char *path);
char *remove_leading_slashes (const char *path);

#endif /* filesys/path.h */
