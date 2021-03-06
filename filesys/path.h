#ifndef FILESYS_PATH_H
#define FILESYS_PATH_H

#include <string.h>
#include "filesys/inode.h"
#include "filesys/directory.h"

struct inode *path_to_inode (char *path);
struct dir *get_start_dir (char *path);

#endif /* filesys/path.h */
