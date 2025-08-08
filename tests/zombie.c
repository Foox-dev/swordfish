#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

int main()
{
  pid_t pid = fork();

  if (pid < 0)
  {
    perror("fork failed");
    exit(1);
  }

  if (pid == 0)
  {
    // Child
    printf("Child (PID %d) exiting\n", getpid());
    exit(0);
  }
  else
  {
    // Parent
    printf("Parent (PID %d), child PID: %d\n", getpid(), pid);
    printf("Sleeping... Check for zombie with `ps aux | grep Z`\n");
    sleep(60); // Keep parent alive, child becomes zombie
  }

  return 0;
}
