#include "userprog/syscall.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include "userprog/fd.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "devices/input.h"
#include "devices/shutdown.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "vm/frame.h"
#include "vm/mmap.h"
#include "vm/page.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/path.h"

/* Identity mapping between process PIDs and thread TIDs. */
typedef tid_t pid_t;

static void syscall_handler (struct intr_frame *);

static void syscall_halt (void);
static void syscall_exit (int status);
static pid_t syscall_exec (const char *cmd_line);
static int syscall_wait (tid_t tid);
static bool syscall_create (const char *file_name, unsigned initial_size);
static bool syscall_remove (const char *file_name);
static int syscall_open (const char *file_name);
static int syscall_filesize (int fd);
static int syscall_read (int fd, void *buf, unsigned size);
static int syscall_write (int fd, const void *buf, unsigned size);
static void syscall_seek (int fd, unsigned position);
static unsigned syscall_tell (int fd);
static void syscall_close (int fd);
static mapid_t syscall_mmap (int fd, void *addr);
static void syscall_munmap (mapid_t mapid);
static bool syscall_mkdir (const char *dir_path);
static bool syscall_chdir (const char *dir_path);
static bool syscall_readdir (int fd, char *name);
static int syscall_inumber (int fd);

static uintptr_t read_frame (struct intr_frame *, int arg_offset);
static void write_frame (struct intr_frame *, uintptr_t ret_value);

static void check_usr_str (const char *usr_ptr);
static void check_usr_addr (const void *start_ptr, int num_bytes);
static void check_usr_ptr (const void *usr_ptr);

/* Initializes the system call handler and lock for filesystem
   system calls. */
void
syscall_init (void) 
{
  lock_init (&filesys_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* Terminates the user program with exit code STATUS. */
void
exit_error (int status)
{
  syscall_exit (status);
}

/* Dispatch to appropriate system call based on syscall number
   in the interrupt frame. */
static void
syscall_handler (struct intr_frame *f) 
{
  /* Save user stack pointer to thread struct in order to handle
     potential page faults in the kernel. */
  thread_current ()->esp = f->esp;
  
  int syscall_num = (int) read_frame (f, 0);
  switch (syscall_num) {
    case SYS_HALT:
      syscall_halt ();
      break;
    case SYS_EXIT:
      {
        int status = (int) read_frame (f, 1);

        syscall_exit (status);
        break;
      }
    case SYS_EXEC:
      {
        const char *cmd_line = (const char *) read_frame (f, 1);
        check_usr_str (cmd_line);

        pid_t pid = syscall_exec (cmd_line);
        write_frame (f, pid);
        break;
      }
    case SYS_WAIT:
      {
        tid_t tid = (tid_t) read_frame (f, 1);

        int exit_status = syscall_wait (tid);
        write_frame (f, exit_status);
        break;
      }
    case SYS_CREATE:
      {
        const char *file_name = (const char *) read_frame (f, 1);
        unsigned initial_size = (unsigned) read_frame (f, 2);
        check_usr_str (file_name);

        bool create_succeeded = syscall_create (file_name, initial_size);
        write_frame (f, create_succeeded);
        break;
      }
    case SYS_REMOVE:
      {
        const char *file_name = (const char *) read_frame (f, 1);
        check_usr_str (file_name);

        bool remove_succeeded = syscall_remove (file_name);
        write_frame (f, remove_succeeded);
        break;
      }
    case SYS_OPEN:
      {
        const char *file_name = (const char *) read_frame (f, 1);
        check_usr_str (file_name);

        int fd = syscall_open (file_name);
        write_frame (f, fd);
        break;
      }
    case SYS_FILESIZE:
      {
        int fd = (int) read_frame (f, 1);

        int filesize = syscall_filesize (fd);
        write_frame (f, filesize);
        break;
      }
    case SYS_READ:
      {
        int fd = (int) read_frame (f, 1);
        void *buf = (void *) read_frame (f, 2);
        unsigned size = (unsigned) read_frame (f, 3);

        /* Check that all page addresses of BUF are valid. */
        for (unsigned i = 0; i < size; i += PGSIZE)
          {
            uint8_t *buf_pg = (uint8_t *) buf + i;
            check_usr_ptr (buf_pg);
          }

        int bytes_read = syscall_read (fd, buf, size);
        write_frame (f, bytes_read);
        break;
      }
    case SYS_WRITE:
      {
        int fd = (int) read_frame (f, 1);
        const void *buf = (const void *) read_frame (f, 2);
        unsigned size = (unsigned) read_frame (f, 3);

        /* Check that all page addresses of BUF are valid. */
        for (unsigned i = 0; i < size; i += PGSIZE)
          {
            uint8_t *buf_pg = (uint8_t *) buf + i;
            check_usr_ptr (buf_pg);
          }

        int bytes_written = syscall_write (fd, buf, size);
        write_frame (f, bytes_written);
        break;
      }
    case SYS_SEEK:
      {
        int fd = (int) read_frame (f, 1);
        unsigned position = (unsigned) read_frame (f, 2);

        syscall_seek (fd, position);
        break;
      }
    case SYS_TELL:
      {
        int fd = (int) read_frame (f, 1);

        unsigned position = syscall_tell (fd);
        write_frame (f, position);
        break;
      }
    case SYS_CLOSE:
      {
        int fd = (int) read_frame (f, 1);

        syscall_close (fd);
        break;
      }
    case SYS_MMAP:
      {
        int fd = (int) read_frame (f, 1);
        void *addr = (void *) read_frame (f, 2);
        check_usr_ptr (addr);

        mapid_t mapid = syscall_mmap (fd, addr);
        write_frame (f, mapid);
        break;
      }
    case SYS_MUNMAP:
      {
        mapid_t mapid = (mapid_t) read_frame (f, 1);

        syscall_munmap (mapid);
        break;
      }
    case SYS_MKDIR:
      {
        const char *dir = (const char *) read_frame (f, 1);
        check_usr_str (dir);

        bool success = syscall_mkdir (dir);
        write_frame (f, success);
        break;
      }
    case SYS_CHDIR:
      {
        const char *dir = (const char *) read_frame (f, 1);
        check_usr_str (dir);

        bool success = syscall_chdir (dir);
        write_frame (f, success);
        break;
      }
    case SYS_READDIR:
      {
        int fd = (int) read_frame (f, 1);
        char *dir = (char *) read_frame (f, 2);
        check_usr_str (dir);

        bool success = syscall_readdir (fd, dir);
        write_frame (f, success);
        break;
      }
    case SYS_INUMBER:
      {
        int fd = (int) read_frame (f, 1);

        int inumber = syscall_inumber (fd);
        write_frame (f, inumber);
        break;
      }
    default:
      break;
  }
}

/* Checks address at intr_frame->esp + offset and returns value
   as uintptr_t if valid. Caller must cast value to desired type. */
static uintptr_t
read_frame (struct intr_frame *f, int arg_offset)
{
  void *addr = f->esp + PTR_SIZE * arg_offset;
  check_usr_addr (addr, PTR_SIZE);
  return *(uintptr_t *) addr;
}

/* Write return value of system call to intr_frame->eax. */
static void
write_frame (struct intr_frame *f, uintptr_t ret_value)
{
  f->eax = ret_value;
}

/* Validates user string. Checks that all characters in
   string are at valid memory locations. */
static void
check_usr_str (const char *usr_ptr)
{
  check_usr_ptr (usr_ptr);
  char *curr = (char *) usr_ptr;

  while (*curr++ != 0)
    check_usr_ptr (curr);
}

/* Validates user pointer. Checks that pointer is a valid user
   vaddr. Exits and terminates the process if check fails. */
static void 
check_usr_addr (const void *usr_ptr, int num_bytes)
{
  for (int i = 0; i < num_bytes; i++)
    check_usr_ptr ((char *) usr_ptr + i);
}

/* Validates user pointer. Checks that pointer is a valid
   user vaddr. If the pointer is not mapped to physical memory,
   the page fault handler will fetch the page if it is mapped.
   Exits and terminates process if the check fails. */
static void 
check_usr_ptr (const void *usr_ptr)
{
  if (!is_user_vaddr (usr_ptr))
    syscall_exit (-1);
}

/* Halts the operating system and powers down the machine. */
static void
syscall_halt (void)
{
  shutdown_power_off ();
}

/* Executes the user program stored in the executable specified by
   the first argument in CMD_LINE. Returns the PID of the user process
   if successful and -1 on failure. */
static pid_t
syscall_exec (const char *cmd_line)
{
  int len = strlen (cmd_line);

  pin_frames (cmd_line, len);
  pid_t pid = process_execute (cmd_line);
  unpin_frames (cmd_line, len);

  return pid;
}

/* Tries to get child p_info, down semaphore, and get/return exit status. 
   Returns -1 if no child with given TID or if already waited on child. */
static int
syscall_wait (tid_t tid)
{
  return process_wait (tid);
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

  printf ("%s: exit(%d)\n", t->name, status);

  /* Internally calls process_exit() and cleans up memory. */
  thread_exit (); 
}

/* Creates a new file called FILE initially INITIAL_SIZE bytes 
   in size. Returns true if successful, false otherwise. */
static bool
syscall_create (const char *file_name, unsigned initial_size)
{
  int len = strlen (file_name);

  lock_acquire (&filesys_lock);

  pin_frames (file_name, len);
  bool create_succeeded = filesys_create (file_name, initial_size, FILE);
  unpin_frames (file_name, len);

  lock_release (&filesys_lock);
  return create_succeeded;
}

/* Deletes the file called FILE. Returns true if successful, false
   false otherwise. A file may be removed regardless of whether it
   is open or closed, and removing an open file does not close it. */
static bool
syscall_remove (const char *file_name)
{
  int len = strlen (file_name);

  lock_acquire (&filesys_lock);

  pin_frames (file_name, len);
  bool remove_succeeded = filesys_remove (file_name);
  unpin_frames (file_name, len);

  lock_release (&filesys_lock);
  return remove_succeeded;
}

/* Opens the file called FILE. Returns a nonnegative integer handle
   (the file descriptor), or -1 if the file could not be opened. */
static int
syscall_open (const char *file_name)
{
  int len = strlen (file_name);

  lock_acquire (&filesys_lock);

  pin_frames (file_name, len);
  struct file *open_file = filesys_open (file_name);
  unpin_frames (file_name, len);

  lock_release (&filesys_lock);
  if (open_file == NULL)
    return -1;

  /* Allocate new fd_entry struct and add to fd_list of process. */
  struct thread *t = thread_current ();
  struct fd_entry *fd_entry = malloc (sizeof (struct fd_entry));
  if (fd_entry == NULL)
    PANIC ("syscall_open: malloc failed for fd_entry.");

  fd_entry->fd = t->fd_counter++;
  fd_entry->file = open_file;
  list_push_back (&t->fd_list, &fd_entry->elem);

  return fd_entry->fd;
}

/* Returns the size, in bytes, of the file open as FD. Returns 0
   if FD not associated with any open file. */
static int
syscall_filesize (int fd)
{
  struct file *open_file = fd_to_file (fd);
  if (open_file == NULL)
    return 0;

  lock_acquire (&filesys_lock);
  int filesize = file_length (open_file);
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

  struct file *open_file = fd_to_file (fd);
  if (open_file == NULL)
    return -1;

  lock_acquire (&filesys_lock);

  /* Acquire locks on frames containing BUF to prevent interference
     from the page eviction policy. Release locks after file_read(). */
  pin_frames (buf, size);
  bytes_read = file_read (open_file, buf, size);
  unpin_frames (buf, size);

  lock_release (&filesys_lock);
  return bytes_read;
}

/* Writes SIZE bytes from BUF to the file open as FD. Returns the
   number of bytes actually written, which may be less than SIZE if
   some bytes could not be written (e.g., write past end-of-file).
   FD 1 writes to the console. Returns 0 on error (e.g., no open
   file associated with FD). */
static int
syscall_write (int fd, const void *buf, unsigned size)
{
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

  struct file *open_file = fd_to_file (fd);
  if (open_file == NULL)
    return 0;
  if (open_file->inode->type == DIR)
    return -1;

  lock_acquire (&filesys_lock);

  /* Acquire locks on frames containing BUF to prevent interference
     from the page eviction policy. Release locks after file_write(). */
  pin_frames (buf, size);
  bytes_written = file_write (open_file, buf, size);
  unpin_frames (buf, size);

  lock_release (&filesys_lock);
  return bytes_written;
}

/* Changes the next byte to be read or written in file open
   as FD to POSITION, expressed in bytes from the beginning
   of the file. Returns without action if no open file is
   associated with FD. */
static void
syscall_seek (int fd, unsigned position)
{
  struct file *open_file = fd_to_file (fd);
  if (open_file == NULL)
    return;

  lock_acquire (&filesys_lock);
  file_seek (open_file, position);
  lock_release (&filesys_lock);
}

/* Returns the position of the next byte to be read or written
   in file open as FD, expressed in bytes from the beginning
   of the file. Returns 0 if no open file is associated with FD. */
static unsigned
syscall_tell (int fd)
{
  struct file *open_file = fd_to_file (fd);
  if (open_file == NULL)
    return 0;

  lock_acquire (&filesys_lock);
  unsigned position = file_tell (open_file);
  lock_release (&filesys_lock);
  return position;
}

/* Close file descriptor FD for the current process Returns without
   action if no open file associated with FD. */
static void
syscall_close (int fd)
{
  struct file *open_file;
  struct thread *t = thread_current ();

  /* Find the fd_entry corresponding to FD in fd_list of process. */
  struct list_elem *fd_elem;
  for (fd_elem = list_begin (&t->fd_list); fd_elem != list_end (&t->fd_list);
       fd_elem = list_next (fd_elem))
    {
      struct fd_entry *entry = list_entry (fd_elem, struct fd_entry, elem);
      if (entry->fd == fd)
        {
          open_file = entry->file;
          lock_acquire (&filesys_lock);
          file_close (open_file);
          lock_release (&filesys_lock);
          list_remove (fd_elem);
          free (entry);
          entry = NULL;

          return;
        }
    }
}

/* Map the file with given fd to provided address. 
   Return -1 on any kind of bad input. */
static mapid_t
syscall_mmap (int fd, void *addr)
{
  return mmap (fd, addr);
}

/* Unmap the mapping with the given mapid and
   free the mappings associated resources. */
static void
syscall_munmap (mapid_t mapid)
{
  munmap (mapid);
}

/* Creates a new directory given path DIR_PATH. Returns false if
   directory creation failed for any reason. */
static bool
syscall_mkdir (const char *dir_path)
{
  return filesys_create (dir_path, 16 * sizeof (struct dir_entry), DIR);
}

/* Changes thread's current working directory to that of
   the provided DIR_PATH if the path's inode exists. Returns 
   false, if it doesn't exist or if the directory has been
   removed. */
static bool
syscall_chdir (const char *dir_path)
{
  struct inode *inode = path_to_inode ((char *) dir_path);
  if (inode == NULL)
    return false;

  lock_acquire (&inode->lock);

  /* Fail call if inode removed. */
  if (inode->removed)
    {
      lock_release (&inode->lock);
      return false;
    }

  /* Close current cwd inode and set to new one. */
  inode_close (thread_current ()->cwd_inode);
  thread_current ()->cwd_inode = inode;
  lock_release (&inode->lock);
  
  return true;
}

/* Reads name of next dir_entry in directory associated with FD 
   into NAME. Returns true if dir_entry was found and false if 
   otherwise. Returns false if fd isn't associated with open 
   directory. */
static bool 
syscall_readdir (int fd, char *name)
{
  struct file *open_file = fd_to_file (fd);
  if (open_file == NULL)
    return false;
  if (open_file->inode->type != DIR)
    return false;
  
  /* Ensure correct dir_entry offset. */
  ASSERT (open_file->pos % sizeof (struct dir_entry) == 0);

  struct dir *dir = malloc (sizeof (struct dir));
  dir->pos = open_file->pos;
  dir->inode = open_file->inode;
  
  bool success = dir_readdir (dir, name);
  open_file->pos += sizeof (struct dir_entry);

  free (dir);
  return success;
}



/* Returns sector number of inode associated with
   FD. Returns -1 if no open file/directory associated
   with FD. */
static int 
syscall_inumber (int fd)
{
  /* Reopen file to avoid inode being freed during call. */
  struct file *file = file_reopen (fd_to_file (fd));
  if (file == NULL)
    return -1;
  
  int inumber = file->inode->sector;
  file_close (file);
  return inumber;
}