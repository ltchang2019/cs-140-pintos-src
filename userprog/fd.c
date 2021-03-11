#include "userprog/fd.h"
#include "filesys/file.h"
#include "threads/malloc.h"
#include "threads/thread.h"

/* Returns a pointer to the file associated with FD in current
   process's set of open file descriptors, or NULL if none. */
struct file *
fd_to_file (int fd)
{
  struct thread *t = thread_current ();
  struct file *file = NULL;
  struct list_elem *fd_elem;

  for (fd_elem = list_begin (&t->fd_list); fd_elem != list_end (&t->fd_list);
       fd_elem = list_next (fd_elem))
    {
      struct fd_entry *entry = list_entry (fd_elem, struct fd_entry, elem);
      if (entry->fd == fd)
        {
          file = entry->file;
          break;
        }
    }

  return file;
}

/* Closes all open file descriptors of a process and deallocates
   resources of process fd_list. */
void
free_fd_list (void)
{
  struct file *file;
  struct thread *t = thread_current ();

  while (!list_empty (&t->fd_list))
    {
      struct list_elem *fd_elem = list_pop_front (&t->fd_list);
      struct fd_entry *entry = list_entry (fd_elem, struct fd_entry, elem);
      file = entry->file;
      
      file_close (file);
      list_remove (fd_elem);
      free (entry);
      entry = NULL;
    }
}
