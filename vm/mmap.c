#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"
#include "userprog/fd.h"
#include "vm/mmap.h"
#include "vm/page.h"

static struct mmap_entry *mapid_to_mmap_entry (mapid_t mapid);
static void munmap_by_mmap_entry (struct mmap_entry *entry, struct thread *t);

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
  
  if (filesize == 0)
    return -1;
  if (!is_valid_mmap_region (addr, filesize))
    return -1;
  
  struct mmap_entry *me = malloc (sizeof (struct mmap_entry));
  me->mapid = t->mapid_counter++;
  me->uaddr = addr;
  list_push_back (&t->mmap_list, &me->elem);

  for (int ofs = 0; ofs < filesize; ofs += PGSIZE)
    {
      lock_acquire (&filesys_lock);
      struct file *fresh_file = file_reopen (file);
      lock_release (&filesys_lock);

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

  munmap_by_mmap_entry (me, t);
}

static void 
munmap_by_mmap_entry (struct mmap_entry *entry, struct thread *t)
{
  struct file *file = spte_lookup (entry->uaddr)->file;
  lock_acquire (&filesys_lock);
  int filesize = file_length (file);
  lock_release (&filesys_lock);
  
  for (int ofs = 0; ofs < filesize; ofs += PGSIZE)
    {
      void *curr_uaddr = entry->uaddr + ofs;
      struct spte *spte = spte_lookup (curr_uaddr);
      ASSERT (spte != NULL);

      if (pagedir_is_dirty (t->pagedir, curr_uaddr))
        {
           lock_acquire (&filesys_lock);
           file_write_at (spte->file, curr_uaddr, spte->page_bytes, spte->ofs);
           lock_release (&filesys_lock);
        }
      spt_delete (&t->spt, &spte->elem);
      free (spte);

    //   TODO: NOT YET FREEING UNDERLYING PAGES FOR MMAPPED FILE
    //   void *kaddr = pagedir_get_page (t->pagedir, curr_uaddr);
    //   printf ("KADDR: %p", kaddr);
    //   frame_free_page (kaddr);
    }
}

void 
munmap_all (void)
{
  struct thread *t = thread_current ();

  while (!list_empty (&t->mmap_list))
    {
       struct list_elem *elem = list_pop_front (&t->mmap_list);
       struct mmap_entry *me = list_entry (elem, struct mmap_entry, elem);
       munmap_by_mmap_entry (me, t);
    }
}
