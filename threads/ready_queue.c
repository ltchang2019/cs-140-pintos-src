#include <list.h>
#include "threads/interrupt.h"
#include "threads/ready_queue.h"

/* Takes ready_queue and initializes its bins. */
void
ready_queue_init (struct ready_queue *q)
{
    for(int i = 0; i < NUM_QUEUES; i++)
        list_init (&q->queues[i]);
}

/* Returns true if ready queue holds zero elements and false if otherwise. */
bool
ready_queue_empty (struct ready_queue *q)
{
    return ready_queue_size (q) == 0;
}

/* Returns number of elements in of ready queue. Throws if num_elems < 0. */
bool
ready_queue_size (struct ready_queue *q)
{
    ASSERT (q->num_elems >= 0);

    return q->num_elems;
}

/* Inserts thread into ready queue based on its priority. */
void 
ready_queue_insert (struct ready_queue *q, struct thread *t)
{
    list_push_back (&q->queues[t->curr_priority], &t->elem);
    q->num_elems++;
}

/* Removes given thread from ready queue. Throws if size == 0. */
void
ready_queue_remove (struct ready_queue *q, struct thread *t)
{
    ASSERT (ready_queue_size (q) > 0);

    list_remove (&t->elem);
    q->num_elems--;
}

/* Returns highest available thread in ready queue. Throws if ready queue empty. */
struct thread *
ready_queue_front (struct ready_queue *q)
{
    ASSERT (!ready_queue_empty (q));

    for (int i = NUM_QUEUES - 1; i >= 0; i--)
        {
            if (list_empty (&q->queues[i]))
                continue;
            
            struct list_elem *e = list_front (&q->queues[i]);
            return list_entry (e, struct thread, elem);
        }

    NOT_REACHED ();
}