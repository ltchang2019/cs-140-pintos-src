#include "userprog/syscall.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "devices/input.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include <stdio.h>
#include <syscall-nr.h>

/* Mapping between process PIDs and thread TIDs. */
typedef tid_t pid_t;

/* Lock for access to the filesys interface. */
struct lock filesys_lock;

static void syscall_handler (struct intr_frame *);

static void check_usr_str (const char *usr_ptr);
static void check_usr_ptr_mult (const void *start_ptr, int num_bytes);
static void check_usr_ptr (const void *usr_ptr); // extract to user err lib?
static pid_t syscall_exec (const char *cmd_line);
static void syscall_exit (int status);
static int syscall_wait (tid_t tid);
static bool syscall_create (const char *file, unsigned initial_size);
static bool syscall_remove (const char *file);
static int syscall_open (const char *file);
static int syscall_filesize (int fd);
static int syscall_read (int fd, void *buf, unsigned size);
static int syscall_write (int fd, const void *buf, unsigned size);
static void syscall_seek (int fd, unsigned position);
static unsigned syscall_tell (int fd);
static void syscall_close (int fd);

// keep in interrupt.c and import err lib to check?
static uintptr_t read_frame (struct intr_frame *, int byte_offset);
static void free_child_p_info_list (void);
static void write_frame (struct intr_frame *, uintptr_t ret_value);

static struct file *fd_to_file (int fd);
static void fd_close_all (void);

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
      shutdown_power_off ();
      break;
    case SYS_EXIT:
      {
        int status = (int)read_frame (f, PTR_SIZE * 1);
        syscall_exit (status);
        break;
      }
    case SYS_EXEC:
      {
        const char *cmd_line = (const char *)read_frame (f, PTR_SIZE * 1);
        pid_t pid = syscall_exec (cmd_line);
        write_frame (f, pid);
        break;
      }
    case SYS_WAIT:
      {
        tid_t tid = (tid_t)read_frame (f, PTR_SIZE * 1);
        int exit_status = syscall_wait (tid);
        write_frame (f, exit_status);
        break;
      }
    case SYS_CREATE:
      {
        const char *file = (const char *)read_frame (f, PTR_SIZE * 1);
        unsigned initial_size = (unsigned)read_frame (f, PTR_SIZE * 2);
        bool create_succeeded = syscall_create (file, initial_size);
        write_frame (f, create_succeeded);
        break;
      }
    case SYS_REMOVE:
      {
        const char *file = (const char *)read_frame (f, PTR_SIZE * 1);
        bool remove_succeeded = syscall_remove (file);
        write_frame (f, remove_succeeded);
        break;
      }
    case SYS_OPEN:
      {
        const char *file = (const char *)read_frame (f, PTR_SIZE * 1);
        int fd = syscall_open (file);
        write_frame (f, fd);
        break;
      }
    case SYS_FILESIZE:
      {
        int fd = (int)read_frame (f, PTR_SIZE * 1);
        int filesize = syscall_filesize (fd);
        write_frame (f, filesize);
        break;
      }
    case SYS_READ:
      {
        int fd = (int)read_frame (f, PTR_SIZE * 1);
        void *buf = (void *)read_frame (f, PTR_SIZE * 2);
        unsigned size = (unsigned)read_frame (f, PTR_SIZE * 3);
        int bytes_read = syscall_read (fd, buf, size);
        write_frame (f, bytes_read);
        break;
      }
    case SYS_WRITE:
      {
        int fd = (int)read_frame (f, PTR_SIZE * 1);
        const void *buf = (const void *)read_frame (f, PTR_SIZE * 2);
        unsigned size = (unsigned)read_frame (f, PTR_SIZE * 3);
        int bytes_written = syscall_write (fd, buf, size);
        write_frame (f, bytes_written);
        break;
      }
    case SYS_SEEK:
      {
        int fd = (int)read_frame (f, PTR_SIZE * 1);
        unsigned position = (unsigned)read_frame (f, PTR_SIZE * 2);
        syscall_seek (fd, position);
        break;
      }
    case SYS_TELL:
      {
        int fd = (int)read_frame (f, PTR_SIZE * 1);
        unsigned position = syscall_tell (fd);
        write_frame (f, position);
        break;
      }
    case SYS_CLOSE:
      {
        int fd = (int)read_frame (f, PTR_SIZE * 1);
        syscall_close (fd);
        break;
      }
    default:
      break;
  }
}

/* Checks address at intr_frame->esp + offset and returns
   value as uintptr_t if valid. User must cast value to desired type. */
static uintptr_t
read_frame (struct intr_frame *f, int byte_offset)
{
  void *addr = f->esp + byte_offset;
  check_usr_ptr_mult (addr, 4);
  return *(uintptr_t *)addr;
}

/* Traverse through current thread's child_p_info_list
   and free all p_info structs. */
static void
free_child_p_info_list (void)
{
  struct thread *t = thread_current ();
  while (!list_empty (&t->child_p_info_list))
    {
      struct list_elem *curr = list_pop_front (&t->child_p_info_list);
      struct p_info *p_info = list_entry (curr, struct p_info, elem);
      list_remove (curr);
      free (p_info);
    }
}

/* Write return value of system call to intr_frame->eax. */
static void
write_frame (struct intr_frame *f, uintptr_t ret_value)
{
  f->eax = ret_value;
}

static void
check_usr_str (const char *usr_ptr)
{
  check_usr_ptr (usr_ptr);

  char *curr = (char *)usr_ptr + 1;
  while (true)
    {
      check_usr_ptr(curr);
      if (*curr == 0)
        break;
      
      curr++;
    }
}

/* Validates user pointer. Checks that pointer is not NULL,
   is a valid user vaddr, and is mapped to physical memory.
   Exits and terminates process if checks fail. */
static void 
check_usr_ptr_mult (const void *usr_ptr, int num_bytes)
{
  for (int i = 0; i < num_bytes; i++)
    {
      check_usr_ptr ((char *)usr_ptr + i);
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
  
  uintptr_t *pd = thread_current ()->pagedir;
  if (pagedir_get_page (pd, usr_ptr) == NULL)
    syscall_exit (-1);
}

/* Returns a pointer to the file associated with FD in current
   process's set of file descriptors, or NULL if none. */
static struct file *
fd_to_file (int fd)
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
  
  return file;
}

static pid_t
syscall_exec (const char *cmd_line)
{
  check_usr_ptr (cmd_line);
  check_usr_str (cmd_line);

  pid_t pid = process_execute (cmd_line);
  return pid;
}

/* Set's p_info exit_status to status and ups semaphore for parent 
   if parent still running (i.e. p_info not NULL). Frees all child
   p_info structs, closes fds, and prints process termination message. */
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
  
  fd_close_all ();

  printf ("%s: exit(%d)\n", t->name, status);
  thread_exit ();
}


/* Tries to get child p_info, down semaphore, and get/return exit status. 
   Returns -1 if no child with given tid or if already waited on child. */
static int
syscall_wait (tid_t tid)
{
  return process_wait (tid);
}

/* Creates a new file called FILE initially INITIAL_SIZE bytes 
   in size. Returns true if successful, false otherwise. */
static bool
syscall_create (const char *file, unsigned initial_size)
{
  check_usr_ptr (file);
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
  check_usr_ptr (file);
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
  lock_acquire (&filesys_lock);
  int filesize = file_length (fd_to_file (fd));
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

  int bytes_read = 0;

  /* Read from the keyboard up to SIZE characters.
     Stop reading on a newline character ('\n'). */
  if (fd == STDIN_FILENO)
    {
      char *buf_pos = buf;
      for (int i = size; i > 0; i--)
        {
          uint8_t next_c = input_getc ();
          if (next_c == '\n')
            break;
          *buf_pos++ = next_c;
          bytes_read++;
        }
      return bytes_read;
    }
  
  /* Fail silently if attempt to read from STDOUT_FILENO. */
  if (fd == STDOUT_FILENO)
    return -1;

  struct file *file = fd_to_file (fd);
  if (file == NULL)
    return -1;
  lock_acquire (&filesys_lock);
  bytes_read = file_read (file, buf, size);
  lock_release (&filesys_lock);
  return bytes_read;
}

/* Writes SIZE bytes from BUF to the file open as FD. Returns the
   number of bytes actually written, which may be less than SIZE if
   some bytes could not be written (e.g., write past end-of-file).
   FD 1 writes to the console. */
static int
syscall_write (int fd, const void *buf, unsigned size)
{
  check_usr_ptr (buf);
  check_usr_ptr (buf + size);

  int bytes_written;

  /* Write to the console. */
  if (fd == STDOUT_FILENO) 
    {
      putbuf (buf, size);
      return size;
    } 

  /* Fail silently if attempt to write to STDIN_FILENO. */
  if (fd == STDIN_FILENO)
    return 0;

  struct file *file = fd_to_file (fd);
  if (file == NULL)
    return 0;
  lock_acquire (&filesys_lock);
  bytes_written = file_write (file, buf, size);
  lock_release (&filesys_lock);
  return bytes_written;
}

/* Changes the next byte to be read or written in file open
   as FD to POSITION, expressed in bytes from the beginning
   of the file. Returns without action if file not open. */
static void
syscall_seek (int fd, unsigned position)
{
  lock_acquire (&filesys_lock);
  file_seek (fd_to_file (fd), position);
  lock_release (&filesys_lock);
}

/* Returns the position of the next byte to be read or written
   in file open as FD, expressed in bytes from the beginning
   of the file */
static unsigned
syscall_tell (int fd)
{
  lock_acquire (&filesys_lock);
  unsigned position = file_tell (fd_to_file (fd));
  lock_release (&filesys_lock);
  return position;
}

/* Close file descriptor FD. */
static void
syscall_close (int fd)
{
  struct file *file;
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
          lock_acquire (&filesys_lock);
          file_close (file);
          lock_release (&filesys_lock);
          list_remove (fd_elem);
          free (entry);
          return;
        }
    }
}

/* Closes all open file descriptors of a process. */
static void
fd_close_all (void)
{
  struct file *file;
  struct thread *t = thread_current ();

  /* Remove and deallocate memory of all fd_entry structs in fd_list. */
  while (!list_empty (&t->fd_list))
    {
      struct list_elem *fd_elem = list_pop_front (&t->fd_list);
      struct fd_entry *entry = list_entry (fd_elem, struct fd_entry, elem);
      file = entry->file;
      lock_acquire (&filesys_lock);
      file_close (file);
      lock_release (&filesys_lock);
      list_remove (fd_elem);
      free (entry);
    }
}