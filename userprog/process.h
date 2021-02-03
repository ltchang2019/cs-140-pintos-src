#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

/* Size of pointer type in bytes. */
#define PTR_SIZE sizeof (uintptr_t)

tid_t process_execute (const char *cmd);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

#endif /* userprog/process.h */
