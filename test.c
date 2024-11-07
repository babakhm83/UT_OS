#include "types.h"
#include "user.h"

// Written by Babak
int
main(int argc, char *argv[]) {
  int pid = getpid();
  if (argc<2)
  {
    printf(2, "usage: test system_call...\n");
    exit();
  }
  if (!strcmp(argv[1],"0"))
    printf(1,"Process ID: %d\n", pid);
  else if(!strcmp(argv[1],"1"))
  {
    if (argc<3)
    {
      printf(2, "usage: test 1 number...\n");
      exit();
    }
    int num=atoi(argv[2]);
    create_palindrome(num);
  }
  else if(!strcmp(argv[1],"2"))
  {
    if (argc<4)
    {
      printf(2, "usage: test 2 src dest...\n");
      exit();
    }
    if(move_file(argv[2], argv[3])==-1)
      printf(2,"move_file system call failed\n");
  }
  else if(!strcmp(argv[1],"3"))
  {
    if (argc<3)
    {
      printf(2, "usage: test 3 pid...\n");
      exit();
    }
    if(sort_syscalls(atoi(argv[2]))==-1)
      printf(2,"sort_syscalls system call failed\n");
  }
  exit();
}