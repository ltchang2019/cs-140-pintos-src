#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <hash.h>
#include <list.h>
#include <stdint.h>
#include "threads/fixed-point.h"
#include "threads/synch.h"
#include "filesys/inode.h"
#include "userprog/p_info.h"

/* Thread ID. */
typedef int tid_t;

/* States in a thread's life cycle. */
enum thread_status
  {
    THREAD_RUNNING,     /* Running thread. */
    THREAD_READY,       /* Not running but ready to run. */
    THREAD_BLOCKED,     /* Waiting for an event to trigger. */
    THREAD_DYING        /* About to be destroyed. */
  };

#define TID_ERROR ((tid_t) - 1)         /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

/* A kernel thread or user process.

   Each thread structure is stored in its own 4 kB page.  The
   thread structure itself sits at the very bottom of the page
   (at offset 0).  The rest of the page is reserved for the
   thread's kernel stack, which grows downward from the top of
   the page (at offset 4 kB).  Here's an illustration:

        4 kB +---------------------------------+
             |          kernel stack           |
             |                |                |
             |                |                |
             |                V                |
             |         grows downward          |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             +---------------------------------+
             |              magic              |
             |                :                |
             |                :                |
             |               name              |
             |              status             |
        0 kB +---------------------------------+

   The upshot of this is twofold:

      1. First, `struct thread' must not be allowed to grow too
         big.  If it does, then there will not be enough room for
         the kernel stack.  Our base `struct thread' is only a
         few bytes in size.  It probably should stay well under 1
         kB.

      2. Second, kernel stacks must not be allowed to grow too
         large.  If a stack overflows, it will corrupt the thread
         state.  Thus, kernel functions should not allocate large
         structures or arrays as non-static local variables.  Use
         dynamic allocation with malloc() or palloc_get_page()
         instead.

   The first symptom of either of these problems will probably be
   an assertion failure in thread_current(), which checks that
   the `magic' member of the running thread's `struct thread' is
   set to THREAD_MAGIC.  Stack overflow will normally change this
   value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
   the run queue (thread.c), or it can be an element in a
   semaphore wait list (synch.c).  It can be used these two ways
   only because they are mutually exclusive: only a thread in the
   ready state is on the run queue, whereas only a thread in the
   blocked state is on a semaphore wait list. */
struct thread
  {

    /* Project 1 additions. */
    tid_t tid;                        /* Thread identifier. */
    enum thread_status status;        /* Thread state. */
    char name[16];                    /* Name (for debugging purposes). */
    uint8_t *stack;                   /* Saved stack pointer. */
    int curr_priority;                /* Current priority. */
    int owned_priority;               /* Priority set by owning thread. */
    struct list_elem allelem;         /* List element in all threads list. */

    /* For priority donation. */
    int num_donations;                /* Number of priority donations. */
    struct list held_locks;           /* List of locks held by thread. */
    struct lock *desired_lock;        /* Lock that thread is blocked on. */

    /* For multi-level feedback queue scheduler. */
    int nice;                         /* Thread generosity with CPU time. */
    fixed32_t recent_cpu;             /* Thread recent CPU usage. */

    /* Shared between thread.c and synch.c. */
    struct list_elem elem;            /* List element. */

    /* Project 2 additions. */  
#ifdef USERPROG  
    uint32_t *pagedir;                /* Owned by userprog/process.c. */
    int fd_counter;                   /* Counter for file descriptors. */
    struct list fd_list;              /* List of open file descriptors. */
    struct file *executable;          /* Reference to executable file. */
    struct list child_p_info_list;    /* List of children p_info structs. */
    struct p_info *p_info;            /* Reference to parent's p_info
                                         struct about this child. */
#endif

    /* Project 3 additions. */
#ifdef VM
    struct hash spt;                  /* Supplemental page table. */
    uint8_t *esp;                     /* Saved stack pointer. */
    size_t mapid_counter;             /* Counter for mapids. */
    struct list mmap_list;            /* List of mmap entries. */
#endif

#ifdef FILESYS
   struct list_elem rw_elem;
   struct inode *cwd_inode;
#endif

    /* Owned by thread.c. */
    unsigned magic;                   /* Detects stack overflow. */
  };

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void mlfqs_tick (int64_t);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);
void thread_wake (struct thread *t);

/* Performs some operation on thread t, given auxiliary data AUX. */
typedef void thread_action_func (struct thread *t, void *aux);
void thread_foreach (thread_action_func *, void *);

void thread_sort_ready_list (void);
int thread_get_priority (void);
void thread_set_priority (int);
void thread_set_donated_priority (struct thread *, int);
bool cmp_priority (const struct list_elem *a, const struct list_elem *b,
                   void *aux UNUSED);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

void calc_load_avg (void);
void increment_recent_cpu (void);
fixed32_t load_avg_coeff (void);
void calc_recent_cpu (struct thread *t, void *aux);
void calc_priority (struct thread *t, void *aux UNUSED);

#endif /* threads/thread.h */
