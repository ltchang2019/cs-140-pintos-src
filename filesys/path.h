#ifndef FILESYS_PATH_H
#define FILESYS_PATH_H

#include <string.h>
#include "filesys/inode.h"
#include "filesys/directory.h"

struct inode *path_to_inode (const char *path);
struct dir *get_start_dir (const char *path);
char *extract_base (const char *path);
char *extract_name (const char *path);

#endif /* filesys/path.h */
