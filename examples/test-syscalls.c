/* test-syscalls.c

   Test functionality of filesystem system calls. */

#include <stdio.h>
#include <syscall.h>

int
main (void) 
{
  // bool create_succeeded;
  // bool remove_succeeded;
  // create_succeeded = create ("quux3.dat", 0);
  // printf ("Create succeeded? %d\n", create_succeeded);
  // remove_succeeded = remove ("quux.dat");

  char buffer[8];
  int bytes_read = read (0, buffer, sizeof buffer);
  printf ("Bytes read: %d\n", bytes_read);

  return EXIT_SUCCESS;

  // if (create_succeeded && remove_succeeded)
  //   return EXIT_SUCCESS;
  // else
  //   return EXIT_FAILURE;
}