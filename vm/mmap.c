#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"
#include "vm/mmap.h"
#include "vm/page.h"

static struct mmap_entry *mapid_to_mmap_entry (mapid_t mapid);
static struct file *fd_to_file (int fd);

/* Returns a pointer to the file associated with FD in current
   process's set of open file descriptors, or NULL if none. */
static struct file *
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

static struct mmap_entry *
mapid_to_mmap_entry (mapid_t mapid)
{
  struct thread *t = thread_current ();
  struct list_elem *elem;

  for (elem = list_begin (&t->mmap_list); elem != list_end (&t->mmap_list);
       elem = list_next (elem))
    {
      struct mmap_entry *me = list_entry (elem, struct mmap_entry, elem);
      if (me->mapid == mapid)
        return me;
    }

  return NULL;
}

/* Ensure all pages to be mmapped are valid addresses
   and don't overlap with an existing page's uaddr. */
static bool 
is_valid_mmap_region (void *start_uaddr, int filesize)
{
  for (int ofs = 0; ofs < filesize; ofs += PGSIZE)
    {
      void *curr_uaddr = start_uaddr + ofs;

      if (curr_uaddr == NULL)
        return false;
      if (((uint32_t) curr_uaddr) % PGSIZE != 0) 
        return false;
      if (!is_user_vaddr (curr_uaddr))
        return false;
      if (spte_lookup (curr_uaddr) != NULL)
        return false;
    }
  return true;
}

// static void 
// munmap_all (void)
// {
//   struct thread *t = thread_current ();
//   struct list_elem *elem;

//   for (elem = list_begin (&t->mmap_list); elem != list_end (&t->mmap_list);
//        elem = list_next (elem))
//     {
//       struct mmap_entry *me = list_entry (elem, struct mmap_entry, elem);
//       syscall_munmap (me->mapid);
//     }
// }

mapid_t
mmap (int fd, void *addr)
{
  struct thread *t = thread_current ();

  struct file *file = fd_to_file (fd);
  if (file == NULL)
    return -1;

  lock_acquire (&filesys_lock);
  int filesize = file_length (file);
  lock_release (&filesys_lock);

  if (!is_valid_mmap_region (addr, filesize))
    return -1;
  
  struct mmap_entry *me = malloc (sizeof (struct mmap_entry));
  me->mapid = t->mapid_counter++;
  me->uaddr = addr;
  list_push_back (&t->mmap_list, &me->elem);

  for (int ofs = 0; ofs < filesize; ofs += PGSIZE)
    {
      struct file *fresh_file = file_reopen (file);
      size_t page_bytes = (ofs + PGSIZE > filesize) ? filesize - ofs : PGSIZE;
      struct spte *spte = spte_create (addr + ofs, DISK, fresh_file,
                                       ofs, page_bytes, true);
      spt_insert (&t->spt, &spte->elem);
    }
  
  return me->mapid;
}

void 
munmap (mapid_t mapid)
{
  struct thread *t = thread_current ();

  struct mmap_entry *me = mapid_to_mmap_entry (mapid);
  ASSERT (me != NULL);

  struct file *file = spte_lookup (me->uaddr)->file;
  lock_acquire (&filesys_lock);
  int filesize = file_length (file);
  lock_release (&filesys_lock);
  
  for (int ofs = 0; ofs < filesize; ofs += PGSIZE)
    {
      void *curr_uaddr = me->uaddr + ofs;
      struct spte *spte = spte_lookup (curr_uaddr);
      ASSERT (spte != NULL);

      if (pagedir_is_dirty (t->pagedir, curr_uaddr))
          file_write_at (spte->file, curr_uaddr, spte->page_bytes, spte->ofs);

      spt_delete (&t->spt, &spte->elem);
      free (spte);

      // void *kaddr = pagedir_get_page (t->pagedir, curr_uaddr);
      // printf ("KADDR: %p", kaddr);
      // frame_free_page (kaddr);
    }
}