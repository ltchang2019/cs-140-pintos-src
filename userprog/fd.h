#ifndef USERPROG_FD_H
#define USERPROG_FD_H

#include <list.h>

/* File descriptor entry. Contains the file descriptor and
   a pointer to it's associated file struct. */
struct fd_entry
  {
    int fd;                           /* Non-negative integer descriptor. */
    struct file *file;                /* Reference to open file. */
    struct list_elem elem;            /* List element. */
  };

struct file *fd_to_file (int fd);
void free_fd_list (void);

#endif /* userprog/fd.h */