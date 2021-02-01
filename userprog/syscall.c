#include "userprog/syscall.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "devices/input.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include <stdio.h>
#include <syscall-nr.h>

/* Mapping between process PIDs and thread TIDs. */
typedef tid_t pid_t;

/* Lock for access to the filesys interface. */
struct lock filesys_lock;

static void syscall_handler (struct intr_frame *);
static void check_usr_ptr (const void *usr_ptr); // extract to user err lib?
static void syscall_exit (int status);
static bool syscall_create (const char *file, unsigned initial_size);
static bool syscall_remove (const char *file);
static int syscall_open (const char *file);
static int syscall_filesize (int fd);
static int syscall_read (int fd, void *buf, unsigned size);
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
    case SYS_EXIT:
      {
        int exit_status = (int)read_frame (f, 4);
        syscall_exit (exit_status);
        break;
      }
    case SYS_EXEC:
    case SYS_WAIT:
    case SYS_CREATE:
      {
        const char *file = (const char *)read_frame (f, 4);
        unsigned initial_size = (unsigned)read_frame (f, 8);
        syscall_create (file, initial_size);
        break;
      }
    case SYS_REMOVE:
      {
        const char *file = (const char *)read_frame (f, 4);
        syscall_remove (file);
        break;
      }
    case SYS_OPEN:
      {
        const char *file = (const char *)read_frame (f, 4);
        syscall_open (file);
        break;
      }
    case SYS_FILESIZE:
      {
        int fd = (int)read_frame (f, 4);
        syscall_filesize (fd);
        break;
      }
    case SYS_READ:
      {
        int fd = (int)read_frame (f, 4);
        void *buf = (void *)read_frame (f, 8);
        unsigned size = (unsigned)read_frame (f, 12);
        syscall_read (fd, buf, size);
        break;
      }
    case SYS_WRITE:
      {
        int fd = (int)read_frame (f, 4);
        const void *buf = (const void *)read_frame (f, 8);
        unsigned size = (unsigned)read_frame (f, 12);
        syscall_write (fd, buf, size);
        break;
      }
    case SYS_SEEK:
    case SYS_TELL:
    case SYS_CLOSE:
    default:
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

/* Creates a new file called FILE initially INITIAL_SIZE bytes 
   in size. Returns true if successful, false otherwise. */
static bool
syscall_create (const char *file, unsigned initial_size)
{
  lock_acquire (&filesys_lock);
  bool create_succeeded = filesys_create (file, initial_size);
  lock_release (&filesys_lock);
  return create_succeeded;
}

/* Deletes the file called FILE. Returns true if successful, false
   false otherwise. A file may be removed regardless of whether it
   is open or closed, and removing an open file does not close it. */
static bool
syscall_remove (const char *file)
{
  lock_acquire (&filesys_lock);
  bool remove_succeeded = filesys_remove (file);
  lock_release (&filesys_lock);
  return remove_succeeded;
}

/* Opens the file called FILE. Returns a nonnegative integer handle
   (the file descriptor), or -1 if the file could not be opened. */
static int
syscall_open (const char *file)
{
  lock_acquire (&filesys_lock);
  struct file *open_file = filesys_open (file);
  lock_release (&filesys_lock);
  if (open_file == NULL)
    return -1;
  
  /* Allocate new fd_entry struct and add to fd_list of process. */
  struct thread *t = thread_current ();
  struct fd_entry *fd_entry = malloc (sizeof (struct fd_entry));
  fd_entry->fd = t->fd_counter++;
  fd_entry->file = open_file;
  list_push_back (&t->fd_list, &fd_entry->elem);

  return fd_entry->fd;
}

/* Returns the size, in bytes, of the file open as FD. */
static int
syscall_filesize (int fd)
{
  struct file *file = NULL;
  struct thread *t = thread_current ();

  /* Find the fd_entry corresponding to FD in fd_list of process. */
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
  
  /* Return 0 if no open file corresponds to FD. */
  if (file == NULL)
    return 0;

  lock_acquire (&filesys_lock);
  int filesize = file_length (file);
  lock_release (&filesys_lock);
  return filesize;
}

/* Reads SIZE bytes from the file open as FD into BUF. Returns the
   number of bytes actually read (0 at end of file), or -1 if the
   file could not be read (due to a condition other than end of
   file). FD 0 reads from the keyboard using input_getc(). */
static int
syscall_read (int fd, void *buf, unsigned size)
{
  check_usr_ptr (buf);
  check_usr_ptr (buf + size);

  /* Read from the keyboard. */
  if (fd == STDIN_FILENO)
    {
      char *buf_pos = buf;
      for (int i = size; i >= 0; i--)
        {
          uint8_t next_c = input_getc ();
          *buf_pos++ = next_c;
        }
      return size;
    }
  
  /* Fail silently if attempt to read from STDOUT_FILENO. */
  if (fd == STDOUT_FILENO)
    return -1;

  struct file *file = NULL;
  struct thread *t = thread_current ();

  /* Find the fd_entry corresponding to FD in fd_list of process. */
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

  /* Return -1 if no open file corresponds to FD. */
  if (file == NULL)
    return 0;

  lock_acquire (&filesys_lock);
  int bytes_read = file_read (file, buf, size);
  lock_release (&filesys_lock);
  return bytes_read;
}

// TODO
static int
syscall_write (int fd, const void *buf, unsigned size)
{
  check_usr_ptr (buf);
  check_usr_ptr (buf + size);

  /* Write to the console. */
  if (fd == STDOUT_FILENO) 
    {
      putbuf (buf, size);
      return size;
    } 

  /* Fail silently if attempt to write to STDIN_FILENO. */
  if (fd == STDIN_FILENO)
    return 0;

  struct file *file = NULL;
  struct thread *t = thread_current ();

  /* Find the fd_entry corresponding to FD in fd_list of process. */
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

  /* Return -1 if no open file corresponds to FD. */
  if (file == NULL)
    return 0;

  lock_acquire (&filesys_lock);
  int bytes_written = file_write (file, buf, size);
  lock_release (&filesys_lock);
  return bytes_written;
}