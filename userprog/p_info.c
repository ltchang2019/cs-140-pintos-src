#include "userprog/p_info.h"
#include "threads/malloc.h"
#include "threads/synch.h"

/* Initialize process info struct for a child thread T. */
void
init_p_info (struct thread *t)
{
  struct p_info *p_info = malloc (sizeof (struct p_info));
  struct semaphore *sema = malloc (sizeof (struct semaphore));
  sema_init (sema, 0);

  p_info->tid = t->tid;
  p_info->exit_status = 0;
  p_info->sema = sema;
  p_info->load_succeeded = false;

  t->p_info = p_info;
  list_push_back (&thread_current ()->child_p_info_list, &p_info->elem);
}

/* Searches through current thread's child_p_info_list for
   process info struct with matching TID. Returns struct if
   found and NULL otherwise. */
struct p_info *
child_p_info_by_tid (tid_t tid)
{
  struct thread *t = thread_current ();
  struct list_elem *curr = list_begin (&t->child_p_info_list);
  struct list_elem *end = list_end (&t->child_p_info_list);
  while (curr != end)
    {
      struct p_info *p_info = list_entry (curr, struct p_info, elem);
      if (p_info->tid == tid) 
        return p_info;
      curr = list_next (curr);
    }
  return NULL;
}