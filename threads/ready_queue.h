#ifndef THREADS_READY_QUEUE_H
#define THREADS_READY_QUEUE_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/thread.h"

#define NUM_QUEUES (PRI_MAX - PRI_MIN + 1)

struct ready_queue 
{
    int num_elems;
    struct list queues[NUM_QUEUES];
};

void ready_queue_init (struct ready_queue *);

bool ready_queue_empty (struct ready_queue *);
int ready_queue_size (struct ready_queue *);

void ready_queue_insert (struct ready_queue *, struct thread *);
void ready_queue_remove (struct ready_queue *, struct thread *);
struct thread *ready_queue_front (struct ready_queue *);

#endif /* threads/ready_queue.h */
