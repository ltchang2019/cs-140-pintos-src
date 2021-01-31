#include "userprog/syscall.h"
#include "userprog/pagedir.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

static void syscall_handler (struct intr_frame *);
static void check_user_ptr (const void *usr_ptr);
static void exit (int status);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  printf ("system call!\n");
  thread_exit ();
}

static void 
check_user_ptr (const void *usr_ptr)
{
  if (usr_ptr == NULL) 
    exit(-1);

  if (!is_user_vaddr(usr_ptr))
    exit(-1);
  
  uint32_t *pd = thread_current ()->pagedir;
  if (pagedir_get_page (pd, usr_ptr) == NULL)
    exit(-1);
}

static void 
exit (int status)
{
}