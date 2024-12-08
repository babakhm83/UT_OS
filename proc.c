#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

char *states_names[] = {
    [UNUSED] "unused",
    [EMBRYO] "embryo",
    [SLEEPING] "sleep ",
    [RUNNABLE] "runble",
    [RUNNING] "run   ",
    [ZOMBIE] "zombie"};

struct
{
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int cpuid()
{
  return mycpu() - cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu *
mycpu(void)
{
  int apicid, i;

  if (readeflags() & FL_IF)
    panic("mycpu called with interrupts enabled\n");

  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i)
  {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc *
myproc(void)
{
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

// PAGEBREAK: 32
//  Look in the process table for an UNUSED proc.
//  If found, change state to EMBRYO and initialize
//  state required to run in the kernel.
//  Otherwise return 0.
static struct proc *
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if (p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  release(&ptable.lock);

  // Allocate kernel stack.
  if ((p->kstack = kalloc()) == 0)
  {
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe *)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint *)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context *)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  // Clear the system call history of the process.
  for (int i = 0; i < sizeof(p->sc) / sizeof(p->sc[0]); i++)
    p->sc[i] = 0;
  p->queue=0;
  p->wait_time=0;
  p->confidence=50;
  p->burst_time=2;
  p->consecutive_runs=0;
  p->arrival=ticks;
  return p;
}

// PAGEBREAK: 32
//  Set up first user process.
void userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();

  initproc = p;
  if ((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0; // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if (n > 0)
  {
    if ((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  else if (n < 0)
  {
    if ((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if ((np = allocproc()) == 0)
  {
    return -1;
  }
  // Copy process state from proc.
  if ((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0)
  {
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for (i = 0; i < NOFILE; i++)
    if (curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;
  acquire(&ptable.lock);

  np->state = RUNNABLE;
  if(pid>2)
    np->queue=2;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if (curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for (fd = 0; fd < NOFILE; fd++)
  {
    if (curproc->ofile[fd])
    {
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->parent == curproc)
    {
      p->parent = initproc;
      if (p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();

  acquire(&ptable.lock);
  for (;;)
  {
    // Scan through table looking for exited children.
    havekids = 0;
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      if (p->parent != curproc)
        continue;
      havekids = 1;
      if (p->state == ZOMBIE)
      {
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        for (int i = 0; i < sizeof(p->sc) / sizeof(p->sc[0]); i++)
          p->sc[i] = 0;
        p->queue=2;
        p->wait_time=0;
        p->confidence=50;
        p->burst_time=2;
        p->consecutive_runs=0;
        p->arrival=ticks;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if (!havekids || curproc->killed)
    {
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock); // DOC: wait-sleep
  }
}

void _aging()
{
  struct proc *p;
  acquire(&ptable.lock);
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if(p->state==RUNNABLE && ++p->wait_time>=800 && p->queue)
    {
      p->queue--;
      p->arrival=ticks;
      p->wait_time=0;
    }
  }
  release(&ptable.lock);
  return;
}

int _RR_scheduler(){
  struct cpu *c = mycpu();
  static int index=0;
  struct proc *p;
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if(p->pid==c->_last_pid_queue[0])
    {
      if (p->state != RUNNABLE || 
      p->queue!=0 || p->consecutive_runs==5)
      {
        if(p->consecutive_runs==5)
          p->consecutive_runs=0;
        continue;
      }
      return p-ptable.proc;
    }
  }
  for (int i=0; i<NPROC; i++)
  {
    index=(index+1)%NPROC;
    if (ptable.proc[index].state != RUNNABLE || ptable.proc[index].queue!=0)
      continue;
    p->consecutive_runs=1;
    return index;
  }
  return -1;
}

int _SJF_scheduler(){
  int p_idx[NPROC];
  struct cpu *c = mycpu();
  int min_val=0,new_min=1e9;
  int idx = 0;
  while(idx<NPROC)
  {
    new_min=1e9;
    int flag=1;
    struct proc *p;
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      if (p->state != RUNNABLE || p->queue!=1)
        continue;
      if(p->pid==c->_last_pid_queue[1])
        return p-ptable.proc;
      if(min_val<p->burst_time){
        new_min=(new_min<p->burst_time) ? new_min : p->burst_time;
        flag=0;
      }
      else if(min_val==p->burst_time){
        p_idx[idx++]=p-ptable.proc;
        flag=0;
      }
    }
    min_val=new_min;
    if(flag)
      break;
  }
  static unsigned long int seed = 1;
  for (int i = 0; i < idx; i++)
  {
    int rand=((unsigned int)(seed / 65536) % 32768)%100;
    seed= (seed+ticks) * 1103515243 + 12345;
    if(rand<ptable.proc[p_idx[i]].confidence)
    {
      ptable.proc[p_idx[i]].consecutive_runs=1;
      return p_idx[i];
    }
  }
  if(idx)
  {
    ptable.proc[p_idx[idx-1]].consecutive_runs=1;
    return p_idx[idx-1];
  }
  return -1;
}

int _FCFS_scheduler(){
  int min_val=1e9,min_idx=-1;
  struct cpu *c = mycpu();
  struct proc *p;
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->state != RUNNABLE || p->queue!=2)
      continue;
    if(p->pid==c->_last_pid_queue[2])
      return p-ptable.proc;
    if(min_val>p->arrival)
    {
      min_val = p->arrival;
      min_idx = p-ptable.proc;
    }
  }
  ptable.proc[min_idx].consecutive_runs=1;
  return min_idx;
}

// PAGEBREAK: 42
//  Per-CPU process scheduler.
//  Each CPU calls scheduler() after setting itself up.
//  Scheduler never returns.  It loops, doing:
//   - choose a process to run
//   - swtch to start running that process
//   - eventually that process transfers control
//       via swtch back to the scheduler.
void scheduler(void)
{
  int p_index,queue=2;
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  for (;;)
  {
    // Enable interrupts on this processor.
    sti();
    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    do
    {
      if(mycpu()->_consecutive_runs_queue==0)
        queue=(queue+1)%3;
      switch (queue)
      {
      case 0:
        p_index=_RR_scheduler();
        break;
      case 1:
        p_index=_SJF_scheduler();
        break;
      case 2:
        p_index=_FCFS_scheduler();
        break;
      
      default:
        p_index=_RR_scheduler();
        break;
      }
      if(p_index==-1)
      {
        c->_last_pid_queue[queue]=-1;
        mycpu()->_consecutive_runs_queue=0;
        if(queue==_NQUEUE-1)
          break;
        continue;
      }
      c->_last_pid_queue[queue]=ptable.proc[p_index].pid;
      p=&ptable.proc[p_index];
      p->wait_time=0;
      p->consecutive_runs=1;
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;
      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }while (c->_consecutive_runs_queue || queue!=_NQUEUE-1);
    release(&ptable.lock);
  }
}
// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void sched(void)
{
  int intena;
  struct proc *p = myproc();

  if (!holding(&ptable.lock))
    panic("sched ptable.lock");
  if (mycpu()->ncli != 1)
    panic("sched locks");
  if (p->state == RUNNING)
    panic("sched running");
  if (readeflags() & FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

int _should_yield(){
  struct proc *p = myproc();
  int queue_time_slice=time_slice*queue_weights[p->queue];
  if(++mycpu()->_consecutive_runs_queue==queue_time_slice)
  {
    mycpu()->_consecutive_runs_queue=0;
    return 1;
  }
  switch (p->queue)
  {
  case 0:
    return (p->consecutive_runs==5);
  case 1:
  case 2:
    return 0;
  
  default:
    return 1;
  }
}

// Give up the CPU for one scheduling round.
void yield(void)
{
  acquire(&ptable.lock); // DOC: yieldlock
  if(_should_yield()){
    myproc()->state = RUNNABLE;
    sched();
  }
  else
    myproc()->consecutive_runs++;
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first)
  {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  if (p == 0)
    panic("sleep");

  if (lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if (lk != &ptable.lock)
  {                        // DOC: sleeplock0
    acquire(&ptable.lock); // DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;
  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if (lk != &ptable.lock)
  { // DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

// PAGEBREAK!
//  Wake up all processes sleeping on chan.
//  The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if (p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == pid)
    {
      p->killed = 1;
      // Wake process from sleep if necessary.
      if (p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

// PAGEBREAK: 36
//  Print a process listing to console.  For debugging.
//  Runs when user types ^P on console.
//  No lock to avoid wedging a stuck machine further.
void procdump(void)
{
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->state == UNUSED)
      continue;
    if (p->state >= 0 && p->state < NELEM(states_names) && states_names[p->state])
      state = states_names[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if (p->state == SLEEPING)
    {
      getcallerpcs((uint *)p->context->ebp + 2, pc);
      for (i = 0; i < 10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}
void create_palindrome(int num) // Babak
{
  int new_num = num;
  int palnum = num;
  while (new_num)
  {
    palnum = palnum * 10 + new_num % 10;
    new_num = new_num / 10;
  }
  cprintf("Palindrome of %d is: %d\n", num, palnum);
  return;
}
void _log_syscall(int num) //Ali
{
  struct proc *curproc = myproc();
  curproc->sc[num - 1]++;
  return;
}
int sort_syscalls(int pid) // Ali
{
  struct proc *p;
  acquire(&ptable.lock);
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == pid)
    {
      for (int i = 0; i < sizeof(p->sc) / sizeof(p->sc[0]); i++)
      {
        if (p->sc[i])
          cprintf("%d %s: %d times\n", i + 1, syscall_names[i], p->sc[i]);
      }
      release(&ptable.lock);
      return 0;
    }
  }
  cprintf("No process with id = %d!\n", pid);
  release(&ptable.lock);
  cprintf("sort_syscalls system call failed\n");
  return -1;
}
// Ali
int get_most_invoked(int pid)
{
  struct proc *p;
  int max = 0;
  int max_i = -1;

  acquire(&ptable.lock);
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == pid)
    {
      for (int i = 0; i < sizeof(p->sc) / sizeof(p->sc[0]); i++)
      {
        if (p->sc[i] > max)
        {
          max = p->sc[i];
          max_i = i;
        }
      }
      if (max == 0)
        cprintf("No system call in process %d!\n", pid);
      else
        cprintf("Most invoked system call in process %d %s: %d times\n", pid, syscall_names[max_i], max);
      release(&ptable.lock);
      return 0;
    }
  }
  cprintf("No process with id = %d!\n", pid);
  release(&ptable.lock);
  cprintf("get_most_invoked_call system call failed\n");
  return -1;
}

// Aidin
int list_all_processes(void)
{
  struct proc *p;
  int sum = 0;
  int p_count = 1;
  int proc_flag = 0;
  acquire(&ptable.lock);
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid)
    {
      proc_flag = 1;
      sum = 0;
      for (int i = 0; i < sizeof(p->sc) / sizeof(p->sc[0]); i++)
      {
        sum += p->sc[i];
      }
      cprintf("%d. %s (id = %d): %d syscalls called\n", p_count, p->name, p->pid, sum);
      p_count++;
    }
  }
  release(&ptable.lock);
  if (proc_flag)
    return 0;
  cprintf("No processes to show\n");
  return -1;
}
// Babak
int set_sjf_info(int pid,int burst,int confidence)
{
  struct proc *p;
  acquire(&ptable.lock);
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == pid)
    {
      p->burst_time=burst;
      p->confidence=confidence;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}
// Babak
int set_queue(int pid,int queue)
{
  if(pid<=0)
  {
    cprintf("Invalid pid\n");
    return -1;
  }
  if(queue>3 || queue < 0)
  {
    cprintf("Invalid queue\n");
    return -1;
  }
  struct proc *p;
  acquire(&ptable.lock);
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == pid)
    {
      if(p->queue==queue)
      {
        cprintf("The process with pid %d is already in queue %d\n",pid,queue);
        return -1;
      }
      p->queue=queue;
      p->arrival=ticks;
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}
// Babak
int report_all_processes(void)
{
  struct proc *p;
  acquire(&ptable.lock);
  cprintf("Name\tPid\tState\tQueue\tWait time\tConfidence\tBurst time\tConsecutive runs\tArrival\n");
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if(p->pid==UNUSED)
      continue;
    cprintf("%s\t%d\t%s\t%d\t%d\t\t%d\t\t%d\t\t%d\t\t\t%d\n", 
    p->name,p->pid,states_names[p->state],p->queue,p->wait_time,p->confidence,p->burst_time,p->consecutive_runs,p->arrival);
  }
  release(&ptable.lock);
  return 0;
}