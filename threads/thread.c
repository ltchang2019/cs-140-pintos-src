#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/ready_queue.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* A container for processes in THREAD_READY state, that is,
   processes that are ready to run but not actually running. */
static struct ready_queue ready_queue;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame 
  {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIMER_FREQ 100          /* Number of timer interrupts per second. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

/* System load average, used by the advanced scheduler in calculations to
   assign priorities to threads. Calculated as an estimate of the average
   number of threads ready to run over the past minute. */
static fixed32_t load_avg;

/* Constants used by the multi-level feedback queue scheduler. */
#define NICE_MAX 20  /* Max nice value of a thread (generous with CPU time). */
#define NICE_MIN -20 /* Min nice value of a thread (a Grinch with CPU time). */

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority,
                         int nice, fixed32_t recent_cpu);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static void thread_check_preempt (void);
static tid_t allocate_tid (void);

struct p_info *child_p_info_by_tid (tid_t tid);
static void init_p_info (struct thread *t);

/* Comparison function for ready queue of threads ready to run.
   Compares by priority and sorts in descending order. */
bool
cmp_priority (const struct list_elem *a, const struct list_elem *b,
              void *aux UNUSED)
{
  int a_pri = list_entry (a, struct thread, elem)->curr_priority;
  int b_pri = list_entry (b, struct thread, elem)->curr_priority;
  return a_pri > b_pri;
}

/* Searches through current thread's child_p_info_list for
   process info struct with matching tid. Returns struct if found
   and NULL if otherwise. */
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

static void
init_p_info (struct thread *t)
{
  struct p_info *p_info = malloc (sizeof(struct p_info));
  struct semaphore *sema = malloc (sizeof(struct semaphore));
  sema_init (sema, 0);

  p_info->tid = t->tid;
  p_info->exit_status = 0;
  p_info->sema = sema;
  p_info->load_succeeded = false;

  t->p_info = p_info;
  if (t != initial_thread)
    list_push_back (&thread_current ()->child_p_info_list, &p_info->elem);
}

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  /* Initializations when using multi-level feedback queue scheduler. */
  if (thread_mlfqs)
    load_avg = 0;

  lock_init (&tid_lock);
  ready_queue_init (&ready_queue);
  list_init (&all_list);

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT, 0, 0);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();

  initial_thread->nice = 0;
  initial_thread->recent_cpu = 0;
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) 
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);

  init_p_info (initial_thread);

  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) 
{
  struct thread *t = thread_current ();

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, unless the multi-level feedback queue scheduler is
   used, in which case priority is calculated according to a
   specific formula. Executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering. */
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  struct thread *cur = thread_current ();

  init_thread (t, name, priority, cur->nice, cur->recent_cpu);
  tid = t->tid = allocate_tid ();
  
  init_p_info (t);

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  /* Add to run queue. */
  thread_unblock (t);

  return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)
   
   This function preempts the running thread if the thread newly
   added to the ready queue has a higher priority. This is important
   to note because: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  ready_queue_insert (&ready_queue, t);
  t->status = THREAD_READY;

  /* Running thread yields to a ready thread of higher priority. */ 
  struct thread *cur = thread_current ();
  if (cur != idle_thread && t->curr_priority > cur->curr_priority)
    thread_yield ();

  intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) 
{
  return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  
  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) 
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable ();
  list_remove (&thread_current ()->allelem);
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread)
    ready_queue_insert (&ready_queue, cur);
  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

/* Preempts the running thread if a thread of higher priority is
   ready to run. */
static void
thread_check_preempt (void)
{
  if (!ready_queue_empty (&ready_queue))
  {
    struct thread *ready_thread = ready_queue_front (&ready_queue);
    if (ready_thread->curr_priority > thread_current ()->curr_priority)
      thread_yield ();
  }
}

/* Puts a sleeping thread that was just woken up into the ready queue. */
void
thread_wake (struct thread *t)
{
  ready_queue_insert (&ready_queue, t);
  t->status = THREAD_READY;
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}

/* Sets the current thread's priority to NEW_PRIORITY. 
   
   This function preempts the running thread if the highest priority
   thread in the ready queue has priority higher than NEW_PRIORITY.
   
   Function call ignored if multi-level feedback queue scheduler is used. */
void
thread_set_priority (int new_priority) 
{
  if (thread_mlfqs)
    return;

  struct thread *t = thread_current ();
  t->owned_priority = new_priority;

  /* Disable interrupts to avoid race condition where you're interrupted, 
     receive donation, then continue with operation and overwrite donation. */
  enum intr_level old_level = intr_disable ();
  if (t->num_donations == 0 || new_priority > t->curr_priority)
    t->curr_priority = new_priority;

  intr_set_level (old_level);
  thread_check_preempt ();
}

/* Resets `ready` thread's curr_priority and readds it to the ready queue.
   No immediate preemption occurs. 
   
   Function call ignored if multi-level feedback queue scheduler is used. */
void 
thread_set_donated_priority (struct thread *t, int priority)
{
  t->curr_priority = priority;
  if (t->status == THREAD_READY)
  {
    ready_queue_remove (&ready_queue, t);
    ready_queue_insert (&ready_queue, t);
  }
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) 
{
  return thread_current ()->curr_priority;
}

/* Updates load_avg, recent_cpu, and thread priorities on necessary 
   timer ticks. Called by thread_tick(). */
void 
mlfqs_tick (int64_t ticks) 
{
  /* Increment recent_cpu of running thread by 1 */
  increment_recent_cpu ();

  /* Update system load average and recent_cpu of each thread
     once per second. */
  if (ticks % TIMER_FREQ == 0)
  {
    calc_load_avg ();
    fixed32_t coeff = load_avg_coeff ();
    thread_foreach (calc_recent_cpu, &coeff);
  }

  /* Update priority of each thread once every fourth tick. */
  if (ticks % TIME_SLICE == 0)
    thread_foreach (calc_priority, NULL);
}


/* Sets the current thread's nice value to NICE. Recalculates
   priority of thread and preempt if necessary. */
void
thread_set_nice (int nice) 
{
  ASSERT (nice >= NICE_MIN && nice <= NICE_MAX);

  struct thread *cur = thread_current ();
  cur->nice = nice;
  calc_priority (cur, NULL);
  thread_check_preempt ();
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) 
{
  return thread_current ()->nice;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
  return fixed_to_two_decimal_format (load_avg);
}

/* Recalculates the new system load average according to the formula:
   load_avg = (59/60)*load_avg + (1/60)*ready_threads */
static const fixed32_t frac_59_60 = (59 * FIXED32_CONST) / 60;
static const fixed32_t frac_1_60 = FIXED32_CONST / 60;
void
calc_load_avg (void) 
{
  int q_size = ready_queue_size (&ready_queue);
  int num_ready = (thread_current () != idle_thread) ? q_size + 1 : q_size;
  fixed32_t term_1 = mul_fixed_fixed (frac_59_60, load_avg);
  fixed32_t term_2 = mul_fixed_int (frac_1_60, num_ready);
  load_avg = term_1 + term_2;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
  return fixed_to_two_decimal_format (thread_current ()->recent_cpu);
}

/* Increment recent_cpu of the running thread by 1, unless it is
   the idle thread. */
void
increment_recent_cpu (void)
{
  struct thread *cur = thread_current ();
  if (cur != idle_thread)
    cur->recent_cpu = add_fixed_int (cur->recent_cpu, 1);
}

/* Returns (2*load_avg)/(2*load_avg + 1), used by calc_recent_cpu. */
fixed32_t
load_avg_coeff (void)
{
  fixed32_t load_avg_numer = mul_fixed_int (load_avg, 2);
  fixed32_t load_avg_denom = add_fixed_int (load_avg_numer, 1);
  return div_fixed_fixed (load_avg_numer, load_avg_denom);
}

/* Recalculates recent_cpu of a thread T according to the formula:
   recent_cpu = (2*load_avg)/(2*load_avg + 1) * recent_cpu + nice */
void
calc_recent_cpu (struct thread *t, void *aux)
{
  fixed32_t term_1 = mul_fixed_fixed (*((fixed32_t *) aux), t->recent_cpu);
  t->recent_cpu = add_fixed_int (term_1, t->nice);
}

/* Recalculates priority of a thread T according to the formula:
   priority = PRI_MAX - (recent_cpu / 4) - (nice * 2)
   
   New priority is always adjusted to lie in the valid range
   PRI_MIN to PRI_MAX. */
static const fixed32_t pri_max_fixed = PRI_MAX * FIXED32_CONST;
void
calc_priority (struct thread *t, void *aux UNUSED)
{
  fixed32_t recent_cpu_4 = div_fixed_int (t->recent_cpu, 4);
  fixed32_t nice_2 = int_to_fixed (t->nice * 2);
  fixed32_t new_pri_fixed = pri_max_fixed - recent_cpu_4 - nice_2;
  int new_pri = fixed_to_int_rzero (new_pri_fixed);
  if (new_pri > PRI_MAX)
    t->curr_priority = PRI_MAX;
  else if (new_pri < PRI_MIN)
    t->curr_priority = PRI_MIN;
  else
    t->curr_priority = new_pri;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      /* Let someone else run. */
      intr_disable ();
      thread_block ();

      /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
      asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);

  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread (void) 
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority,
             int nice, fixed32_t recent_cpu)
{
  enum intr_level old_level;

  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;

  /* If round-robin scheduler used, nice and recent_cpu are
     ignored. If multi-level feedback queue scheduler is used,
     priority is ignored and rather calculated from nice and
     recent_cpu, which are inherited from the parent thread,
     and num_donations is ignored since it is only relevant
     for priority donations. */
  if (!thread_mlfqs)
  {
    t->owned_priority = priority;
    t->curr_priority = priority;
    t->num_donations = 0;
  }
  else
  {
    t->nice = nice;
    t->recent_cpu = recent_cpu;
    calc_priority (t, NULL);
  }
  t->desired_lock = NULL;
  list_init (&t->held_locks);
  t->magic = THREAD_MAGIC;

  #ifdef USERPROG
  list_init (&t->child_p_info_list);

  t->fd_counter = 2;         /* 0 and 1 reserved for stdin and stdout. */
  list_init (&t->fd_list);   /* List of open file descriptors. */
  #endif

  old_level = intr_disable ();
  list_push_back (&all_list, &t->allelem);
  intr_set_level (old_level);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size) 
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) 
{
  if (ready_queue_empty (&ready_queue))
    return idle_thread;
  else
    {
      struct thread *t = ready_queue_pop_front (&ready_queue);
      return t;
    }
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void
schedule (void) 
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (cur != next)
    prev = switch_threads (cur, next);
  thread_schedule_tail (prev);
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);
