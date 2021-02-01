#include "userprog/syscall.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "filesys/filesys.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include <stdio.h>
#include <syscall-nr.h>

typedef tid_t pid_t;

struct lock filesys_lock;

static void syscall_handler (struct intr_frame *);
static void check_usr_ptr (const void *usr_ptr); // extract to user err lib?
static void syscall_exit (int status);
static int syscall_write (int fd, const void *buf, unsigned size);

// keep in interrupt.c and import err lib to check?
static uint32_t read_frame (struct intr_frame *, int offset);

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

// TODO
static void 
syscall_exit (int status UNUSED)
{
  thread_exit ();
}

// TODO
static int
syscall_wait (pid_t pid UNUSED)
{
  while (true) {}
  return 0;
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