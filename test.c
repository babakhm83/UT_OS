#include "types.h"
#include "user.h"
// Written by Babak

void ca2_test(int argc, char *argv[]){
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
      printf(2,"returned -1\n");
  }
  else if(!strcmp(argv[1],"3"))
  {
    if (argc<3)
    {
      printf(2, "usage: test 3 pid...\n");
      exit();
    }
    if(sort_syscalls(atoi(argv[2]))==-1)
      printf(2,"returned -1\n");
  }
  else if(!strcmp(argv[1],"4"))
  {
    if (argc<3)
    {
      printf(2, "usage: test 4 pid...\n");
      exit();
    }
    getpid();
    getpid();
    getpid();
    if(get_most_invoked(atoi(argv[2]))==-1)
      printf(2,"returned -1\n");
  }
  else if(!strcmp(argv[1],"5"))
  {
    if (list_all_processes() == -1)
      printf(2,"returned -1\n");
  }
  exit();
}
void heavy_calculation(){
  for (int i = 0; i < 1e10; i++);
  printf(2,"\nDone\n");
}
void ca3_test(int argc, char *argv[]){
  if (argc<2)
  {
    printf(2, "usage: test system_call...\n");
    exit();
  }
  if (!strcmp(argv[1],"0")){
    fork();
    heavy_calculation();
    wait();
  }
  if (!strcmp(argv[1],"1"))
    report_all_processes();
  exit();
}
int
main(int argc, char *argv[]) {
  if(!strcmp(argv[argc-1],"2"))
    ca2_test(argc-1,argv);
  else if(!strcmp(argv[argc-1],"3"))
    ca3_test(argc-1,argv);
  else
    printf(2, "usage: test ca ...\n");
  exit();
}