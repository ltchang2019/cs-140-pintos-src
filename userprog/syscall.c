#include "userprog/syscall.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "filesys/filesys.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include <stdio.h>
#include <syscall-nr.h>

struct lock filesys_lock;

static void syscall_handler (struct intr_frame *);

static void check_usr_ptr (const void *usr_ptr); // extract to user err lib?
static void syscall_exit (int status);
static int syscall_wait (tid_t tid);
static int syscall_write (int fd, const void *buf, unsigned size);

// keep in interrupt.c and import err lib to check?
static uint32_t read_frame (struct intr_frame *, int offset);
static void free_child_p_info_list (void);

void
syscall_init (void) 
{
  lock_init (&filesys_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  int syscall_num = (int)read_frame (f, 0);
  switch (syscall_num) {
    case SYS_HALT:
      break;
    case SYS_EXIT:
      break;
    case SYS_EXEC:
      break;
    case SYS_WAIT:
      break;
    case SYS_CREATE:
      break;
    case SYS_REMOVE:
      break;
    case SYS_OPEN:
      break;
    case SYS_FILESIZE:
      break;
    case SYS_READ:
      break;
    case SYS_WRITE:
      break;
    case SYS_SEEK:
      break;
    case SYS_TELL:
      break;
    case SYS_CLOSE:
      break;
  }
}

/* Checks address at intr_frame->esp + offset and returns
   value as uint32_t if valid. User must cast value to desired type. */
static uint32_t
read_frame (struct intr_frame *f, int offset)
{
  void *addr = f->esp + offset;
  check_usr_ptr (addr);
  return *(uint32_t *)addr;
}

/* Traverse through current thread's child_p_info_list
   and free all p_info structs. */
static void
free_child_p_info_list (void)
{
  struct thread *t = thread_current ();
  struct list_elem *curr = list_begin (&t->child_p_info_list);
  struct list_elem *end = list_end (&t->child_p_info_list);
  while (curr != end)
    {
      struct p_info *p_info = list_entry (curr, struct p_info, elem);
      free (p_info);
      curr = list_next (curr);
    }
}

/* Validates user pointer. Checks that pointer is not NULL,
   is a valid user vaddr, and is mapped to physical memory.
   Exits and terminates process if checks fail. */
static void 
check_usr_ptr (const void *usr_ptr)
{
  if (usr_ptr == NULL) 
    syscall_exit (-1);

  if (!is_user_vaddr (usr_ptr))
    syscall_exit (-1);
  
  uint32_t *pd = thread_current ()->pagedir;
  if (pagedir_get_page (pd, usr_ptr) == NULL)
    syscall_exit (-1);
}

/* Set's p_info exit_status to status and ups semaphore if 
   parent still running (i.e. p_info not NULL). Frees all child
   p_info structs and prints process termination message. */
static void 
syscall_exit (int status)
{
  struct thread *t = thread_current ();

  /* If parent still running, set exit status and 
     signal to parent with sema_up. */
  if (t->p_info != NULL)
  {
    t->p_info->exit_status = status;
    sema_up (t->p_info->sema);
  }
    
  free_child_p_info_list ();
  
  printf ("%s: exit(%d)\n", t->name, status);
  thread_exit ();
}

/* Tries to get child p_info, down semaphore, and get/return exit status. 
   Returns -1 if no child with given tid or if already waited on child. */
static int
syscall_wait (tid_t tid)
{
  struct p_info *child_p_info = child_p_info_by_tid (tid);

  /* Already waited on child and freed struct. */
  if (child_p_info == NULL) 
    return -1;
  
  /* Down child's semaphore. Once unblocked, free p_info
     struct and return exit status. If this process tries to
     wait on same tid again, it will hit p_info == NULL and 
     return -1 as intended. */
  sema_down (child_p_info->sema);

  int exit_status = child_p_info->exit_status;
  list_remove (&child_p_info->elem);
  free (child_p_info);
  
  return exit_status;
}

// TODO
static int
syscall_write (int fd, const void *buf, unsigned size)
{
  check_usr_ptr (buf);
  if (fd == STDOUT_FILENO) 
  {
    putbuf (buf, size);
    return size;
  }
  else 
  {
    lock_acquire (&filesys_lock);
    // TODO: get file* from current thread's list of file_info structs
    //       and call file_write (file, buf, size)
    lock_release (&filesys_lock);
  }
}