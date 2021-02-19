#ifndef USERPROG_P_INFO_H
#define USERPROG_P_INFO_H

#include <stdint.h>
#include <stdbool.h>
#include <list.h>
#include "threads/thread.h"

typedef int tid_t;

/* Process info struct. Contains information necessary for
   parent and child to communicate with each other about child
   process's load status and exit status. */ 
struct p_info
  {
    tid_t tid;                        /* TID of child process. */
    int exit_status;                  /* Exit status of child. */
    bool load_succeeded;              /* Child process load result. */
    struct semaphore *sema;           /* Synchronization so parent waits
                                         properly for child. */
    struct list_elem elem;            /* List element. */
  };

void init_p_info (struct thread *t);
struct p_info *child_p_info_by_tid (tid_t);

#endif /* userprog/p_info.h */