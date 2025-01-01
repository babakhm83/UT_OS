// Reentrant locks

#include "types.h"
#include "defs.h"
#include "param.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "reentrantlock.h"

void
initreentrantlock(struct reentrantlock *lk, char *name)
{
  initsleeplock(&lk->lk, "reentrant lock");
  lk->name = name;
  lk->locked = 0;
  lk->pid = 0;
  lk->recursion = 0;
}

void
acquirereentrant(struct reentrantlock *lk)
{
  if(!holdingreentrant(lk)){
    acquiresleep(&lk->lk);
    lk->locked = 1;
    lk->pid = myproc()->pid;
  }
  lk->recursion++;
}

void
releasereentrant(struct reentrantlock *lk)
{
  if(!holdingreentrant(lk))
    return;
  if(--lk->recursion == 0){
    lk->locked = 0;
    lk->pid = 0;
    releasesleep(&lk->lk);
  }
}

int
holdingreentrant(struct reentrantlock *lk)
{
  return lk->locked && (lk->pid == myproc()->pid);
}



