// Per-CPU state
struct cpu {
  uchar apicid;                // Local APIC ID
  struct context *scheduler;   // swtch() here to enter scheduler
  struct taskstate ts;         // Used by x86 to find stack for interrupt
  struct segdesc gdt[NSEGS];   // x86 global descriptor table
  volatile uint started;       // Has the CPU started?
  int ncli;                    // Depth of pushcli nesting.
  int intena;                  // Were interrupts enabled before pushcli?
  struct proc *proc;           // The process running on this cpu or null
  int _consecutive_runs_queue; // The number of times a process from the last queue has been running.
  int _current_queue;          // The current queue the cpu is choosing processes from.
  int _syscall_counter;        // Number of system calls, called by a process being run on this CPU
};

extern struct cpu cpus[NCPU];
extern int ncpu;

static const int time_slice = 10;
static const int rr_timeq = 5;
static const int queue_weights[_NQUEUE]={3,2,1};

//PAGEBREAK: 17
// Saved registers for kernel context switches.
// Don't need to save all the segment registers (%cs, etc),
// because they are constant across kernel contexts.
// Don't need to save %eax, %ecx, %edx, because the
// x86 convention is that the caller has saved them.
// Contexts are stored at the bottom of the stack they
// describe; the stack pointer is the address of the context.
// The layout of the context matches the layout of the stack in swtch.S
// at the "Switch stacks" comment. Switch doesn't save eip explicitly,
// but it is on the stack and allocproc() manipulates it.
struct context {
  uint edi;
  uint esi;
  uint ebx;
  uint ebp;
  uint eip;
};

enum procstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

static const char *syscall_names[] = {"fork", "exit", "wait", "pipe", "read", "kill", "exec", "fstat", "chdir", "dup",
                                      "getpid", "sbrk", "sleep", "uptime", "open", "write", "mknod", "unlink", "link", "mkdir", "close",
                                      "create_palindrome", "move_file", "sort_syscalls", "get_most_invoked_syscall", " list_all_processes",
                                      "set_sjf_info", "set_queue", "report_all_processes", "total_syscalls_count", "fibonacci_number"};

// Per-process state
struct proc {
  uint sz;                     // Size of process memory (bytes)
  pde_t* pgdir;                // Page table
  char *kstack;                // Bottom of kernel stack for this process
  enum procstate state;        // Process state
  int pid;                     // Process ID
  struct proc *parent;         // Parent process
  struct trapframe *tf;        // Trap frame for current syscall
  struct context *context;     // swtch() here to run process
  void *chan;                  // If non-zero, sleeping on chan
  int killed;                  // If non-zero, have been killed
  struct file *ofile[NOFILE];  // Open files
  struct inode *cwd;           // Current directory
  char name[16];               // Process name (debugging)
  int sc[sizeof(syscall_names)/sizeof(char*)]; //Babak          // Array storing the number of times each system call is invoked by this process
  int queue;             // The scheduling queue
  int wait_time;         // Total wait time
  int confidence;        // Confidence in burst time
  int burst_time;        // Burst time
  int consecutive_runs;  // Last number of consecutive runs
  int arrival;           // Time of arrival
};

// Process memory is laid out contiguously, low addresses first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap
