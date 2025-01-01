// Reentrant locks for processes which may call recursive functions
struct reentrantlock {
  uint locked;       // Is the lock held?
  struct sleeplock lk;   // spinlock protecting reentrant lock from other processes
  int pid;           // Process holding lock
  int recursion;
  
  // For debugging:
  char *name;        // Name of lock.
};

