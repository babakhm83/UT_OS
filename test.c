#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "reentrantlock.h"
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
  for (int i = 0; i < 1e8; i++);
}
void ca3_test(int argc, char *argv[]){
  if (argc<2)
  {
    printf(2, "usage: test algorithm...\n");
    exit();
  }
  if (!strcmp(argv[1],"0"))
    report_all_processes();
  else if (!strcmp(argv[1],"rr")){
    for (int i = 0; i < 5; i++)
      fork();
    // report_all_processes();
    heavy_calculation();
    for (int i = 0; i < 5; i++)
      wait();
  }
  else if (!strcmp(argv[1],"sjf")){
    int pids[4],bursts[4]={6,3,4,7},confidences[4]={50,50,50,50};
    if((fork())==0){
      heavy_calculation();
      exit();
    }
    for (int i = 0; i < 4; i++)
    {
      if((pids[i]=fork())==0)
      {
        for (int i = 0; i < 1e4; i++);
        exit();
      }
      else
        set_sjf_info(pids[i],bursts[i],confidences[i]);
    }
  }
  else if (!strcmp(argv[1],"set_sjf_info"))
    set_sjf_info(atoi(argv[2]),atoi(argv[3]),atoi(argv[4]));
  else if (!strcmp(argv[1],"set_queue"))
    set_queue(atoi(argv[2]),atoi(argv[3]));
  else if (!strcmp(argv[1],"report_all"))
    report_all_processes();
  exit();
}
void write_alot(int id){
    int fd;
    char file_name[32]="i_test_ca4.txt";
    file_name[0]='1'+id;
    if((fd = open(file_name, O_CREATE | O_WRONLY)) < 0){
      printf(2, "Opening file failed\n");
      exit();
    }
    for (int i = 0; i < 10; i++)
      write(fd,"1",1);
    close(fd);
    exit();
}
void test_fib(int argc, char *argv[]){
  if (argc < 4)
  {
    printf(2, "usage: test 1 fib1 fib2...\n");
    exit();
  }
  int num1 = atoi(argv[2]), num2 = atoi(argv[3]), fib1 = 0, fib2 = 0;
  int pid = fork();
  if (!pid)
  {
    fib1 = fibonacci_number(num1);
    if (fib1 != -1)
      printf(1, "fib %d is %d\n", num1, fib1);
    exit();
  }
  else
    fib2 = fibonacci_number(num2);
  wait();
  if (fib1 != -1)
    printf(1, "fib %d is %d\n", num2, fib2);
  exit();
}
void ca4_test(int argc, char *argv[]){
  if (argc<2)
  {
    printf(2, "usage: test part...\n");
    exit();
  }
  if (!strcmp(argv[1],"0"))
  {
    int pid,n_process=4;
    for (int i = 0; i < n_process; i++)
    {
      pid=fork();
      if(!pid)
        write_alot(i);
    }
    for (int i = 0; i < n_process; i++)
      wait();
    report_syscalls_count();
  }
  if (!strcmp(argv[1],"1"))
  {
    test_fib(argc,argv);
  }
  exit();
}
void ca5_test(int argc, char *argv[])
{
  if (argc < 3)
  {
    printf(2, "usage: test number n_children_processes...\n");
    exit();
  }
  int n=atoi(argv[1]),n_children=atoi(argv[2]),pid,mem_id=0;
  int *shared_mem=(int*)open_sharedmem(mem_id);
  if((int)shared_mem==-1)
  {
    printf(2, "ERROR: open_sharedmem\n");
    exit();
  }
  *shared_mem = 0;
  *(shared_mem+1) = 1;
  for (int i = 0; i < n_children; i++)
  {
    pid = fork();
    if (!pid){
      calculate_factorial(n, mem_id);
      exit();
    }
  }
  for (int i = 0; i < n_children; i++)
    wait();
  printf(1, "fact(%d)=%d\n", (*shared_mem), (*(shared_mem + 1)));
  if (close_sharedmem(mem_id) < 0)
  {
    printf(2, "ERROR: close_sharedmem\n");
    exit();
  }
  exit();
}
int
main(int argc, char *argv[]) {
  if(!strcmp(argv[argc-1],"2"))
    ca2_test(argc-1,argv);
  else if(!strcmp(argv[argc-1],"3"))
    ca3_test(argc-1,argv);
  else if(!strcmp(argv[argc-1],"4"))
    ca4_test(argc-1,argv);
  else if (!strcmp(argv[argc - 1], "5"))
    ca5_test(argc - 1, argv);
  else
    printf(2, "usage: test ... ca\n");
  exit();
}