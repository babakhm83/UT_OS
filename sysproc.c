#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// Takes an integer and prints its palindrome written by Babak
int
sys_create_palindrome(void)
{
  struct proc *curproc = myproc();
  int num = curproc->tf->ecx;
  create_palindrome(num);
  return 0;
}

// Prints systemcalls invoked by a process, sorted by their number written by Babak
int
sys_sort_syscalls(void)
{
  int pid;
  if(argint(0, &pid) < 0)
    return -1;
  return sort_syscalls(pid);
}

// Prints most invoked systemcall by a process, written by Ali
int
sys_get_most_invoked(void)
{
  int pid;
  if(argint(0, &pid) < 0)
    return -1;
  return get_most_invoked(pid);
}

// List all processes and count of their systemcalls, written by Aidin
int 
sys_list_all_processes(void)
{
  return list_all_processes();
}

// Set confidence and burst time for a process
int  
sys_set_sjf_info(void)
{
  int pid,burst,confidence;
  if(argint(0, &pid) < 0)
    return -1;
  if(argint(1, &burst) < 0)
    return -1;
  if(argint(2, &confidence) < 0)
    return -1;
  return set_sjf_info(pid,burst,confidence);
}

// Set queue number of a process
int  
sys_set_queue(void)
{
  int pid,queue;
  if(argint(0, &pid) < 0)
    return -1;
  if(argint(1, &queue) < 0)
    return -1;
  return set_queue(pid,queue);
}

// Print information about a process given its pid
int  
sys_report_all_processes(void)
{
  return report_all_processes();
}

// Return total number of system calls, called by all cpus
int  
sys_report_syscalls_count(void)
{
  return report_syscalls_count();
}

// Returns the nth fibonacci number. This system call is written to test the reentrant lock.
int
sys_fibonacci_number(void)
{
  int num;
  if (argint(0, &num) < 0)
    return -1;
  return fibonacci_number(num);
}

int
sys_open_sharedmem(void)
{
  int id;
  if (argint(0, &id) < 0)
    return -1;
  return open_sharedmem(id);
}

int sys_close_sharedmem(void)
{
  int id;
  if (argint(0, &id) < 0)
    return -1;
  return close_sharedmem(id);
}

int sys_calculate_factorial(void)
{
  int n;
  int mem;
  if (argint(0, &n) < 0 || argint(1, &mem))
    return -1;
  calculate_factorial(n, mem);
  return 0;
}