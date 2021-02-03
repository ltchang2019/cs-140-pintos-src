/* Executes and waits for a single child process. */

#include <syscall.h>

int
main (void) 
{
  wait (exec ("child-simple"));
  return 0;
}
