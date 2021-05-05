#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

// 3 Threads
int nexttid = 1;
struct spinlock tid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// 2.4 Handling Signals
extern void* sig_ret_start(void);
extern void* sig_ret_end(void);

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl) {
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table at boot time.
void
procinit(void)
{
  struct proc *p;
  struct thread *t;
  
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  initlock(&tid_lock, "nexttid");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      for(t = p->p_threads; t < &p->p_threads[NTHREAD]; t++) {
        initlock(&t->lock, "thread");
      }
      // p->kstack = KSTACK((int) (p - proc));
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void) {
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void) {
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

// 3 Threads
// Return the current struct thread *, or zero if none.
struct thread*
mythread(void) {
  push_off();
  struct cpu *c = mycpu();
  struct thread *t = c->thread;
  pop_off();
  return t;
}

int
allocpid() {
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// 3 Threads
int
alloctid() {
  int tid;
  
  acquire(&tid_lock);
  tid = nexttid;
  nexttid = nexttid + 1;
  release(&tid_lock);

  return tid;
}

static void
freethread(struct thread* t)
{
  if(t->kstack)
    kfree((void*)t->kstack);
  t->kstack = 0;
  if(t->user_trap_backup)
    kfree((void*)t->user_trap_backup);
  t->trapframe = 0;
  t->user_trap_backup = 0;
  t->chan = 0;
  t->state = T_UNUSED;
  t->xstate = 0;
  t->tid = 0;
  t->killed = 0;
  t->parent = 0;
}

// 3 Threads
static struct thread*
allocthread(struct proc* p) 
{
  struct thread *t;
  int i = 0;
  for(t = p->p_threads; t < &p->p_threads[NTHREAD]; t++, i++) {
    if (t != mythread()) {
      acquire(&t->lock);
      if(t->state == T_UNUSED) {
        goto found;
      } else {
        release(&t->lock);
      }
    }
  }
  return 0;

  found:
  t->tid = alloctid();
  t->state = T_USED;
  t->tf_index = i;
  t->trapframe = &p->t_trapframes[i];
  t->parent = p;

  // Allocate a trapframe backup page.
  if((t->user_trap_backup = (struct trapframe *)kalloc()) == 0){
    freethread(t);  
    release(&t->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&t->context, 0, sizeof(t->context));
  t->context.ra = (uint64)forkret;

  // Allocate kernel stack.
  if((t->kstack = (uint64)kalloc()) == 0) {
      freethread(t);
      release(&t->lock);
      return 0;
  }
  t->context.sp = t->kstack + PGSIZE;

  return t;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  // 3 Threads
  // // Allocate a threads array
  // if((p->p_threads = (struct thread *)kalloc()) == 0){
  //   freeproc(p);
  //   release(&p->lock);
  //   return 0;
  // }

  // Allocate a threads array.
  if((p->t_trapframes = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // 2.1.2 Updating process creation behavior
  //initialize all signal handlers to be default handler
  int sig;
  for(sig = 0; sig < NSIGS; sig++) {
    p->sig_handlers[sig] = (void*)SIG_DFL;
  }

  struct thread *t;
  // Allocate thread.
  if((t = allocthread(p)) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->t_trapframes)
    kfree((void*)p->t_trapframes);
  p->t_trapframes = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
  p->pending_signals = 0;
  p->sig_mask = 0;

  int sig;
  for(sig = 0; sig < NSIGS; sig++) {
    p->sig_handlers[sig] = (void*)SIG_DFL;
    p->sig_handlers_masks[sig] = 0;
  }

  p->in_signal_handler = 0;
  p->prev_sig_mask = 0;

  struct thread *t;
  for(t = p->p_threads; t < &p->p_threads[NTHREAD]; t++) {
    freethread(t);
  }
}

// Create a user page table for a given process,
// with no user memory, but with trampoline pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe just below TRAMPOLINE, for trampoline.S.
  if(mappages(pagetable, TRAPFRAME(0), PGSIZE,
              (uint64)(p->t_trapframes), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME(0), 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// od -t xC initcode
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
  
  // allocate one user page and copy init's instructions
  // and data into it.
  uvminit(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->t_trapframes->epc = 0;      // user program counter
  p->t_trapframes->sp = PGSIZE;  // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->p_threads[0].state = T_RUNNABLE;

  release(&(p->p_threads[0].lock));
  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *p = myproc();
  acquire(&p->lock);
  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n)) == 0) {
      release(&p->lock);
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  release(&p->lock);
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();
  struct thread *nt;
  struct thread *t = mythread();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  nt = &np->p_threads[0];

  // copy saved user registers.
  *(nt->trapframe) = *(t->trapframe);

  // Cause fork to return 0 in the child.
  nt->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  // 2.1.2 Updating process creation behavior
  np->sig_mask = p->sig_mask;
  np->pending_signals = 0;

//deep copy parent signal handlers
  int sig;
  for(sig = 0; sig < NSIGS; sig++) {
    np->sig_handlers[sig] = p->sig_handlers[sig];
    np->sig_handlers_masks[sig] = p->sig_handlers_masks[sig];
  }

  release(&nt->lock);
  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&nt->lock);
  nt->state = T_RUNNABLE;
  release(&nt->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
  struct proc *p = myproc();

  printf("inside of main exit\n");

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  
  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  printf("before releasing threads\n");

  for(struct thread *t = p->p_threads; t < &p->p_threads[NTHREAD]; t++) {
    acquire(&t->lock);
    t->state = T_ZOMBIE;
    if(t != mythread())
      release(&t->lock);
  }

  printf("after releasing threads\n");

  release(&p->lock);
  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
  struct proc *np;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(np = proc; np < &proc[NPROC]; np++){
      if(np->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&np->lock);

        havekids = 1;
        if(np->state == ZOMBIE){
          // Found one.
          pid = np->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&np->xstate,
                                  sizeof(np->xstate)) < 0) {
            release(&np->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(np);
          release(&np->lock);
          release(&wait_lock);
          return pid;
        }
        release(&np->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || p->killed){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct thread *t;
  struct cpu *c = mycpu();

  // printf("in scheudler cpuid: %d\n", cpuid());
  
  c->proc = 0;
  c->thread = 0;
  for(;;){
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();

    for(p = proc; p < &proc[NPROC]; p++) {
      if(p->state == USED) {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        for(t = p->p_threads; t < &p->p_threads[NTHREAD]; t++) {
          acquire(&t->lock);
          // printf("after acquire tlock pid: %d, cpuid: %d\n", p->pid, cpuid());
          if(t->state == T_RUNNABLE){
            t->state = T_RUNNING;
            c->thread = t;
            c->proc = p;
            // printf("before swtch\n");
            swtch(&c->context, &t->context);
            // printf("after swtch\n");
            // Process is done running for now.
            // It should have changed its p->state before coming back.
            c->proc = 0;
            c->thread = 0;
          }
          // printf("before release tlock pid: %d, cpuid: %d\n", p->pid, cpuid());
          release(&t->lock);
          // printf("before release tlock pid: %d, cpuid: %d\n", p->pid, cpuid());
        }
      }
    }   
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct thread *t = mythread();

  if(!holding(&t->lock))
    panic("sched t->lock");
  if(mycpu()->noff != 1) {
    if (holding(&myproc()->lock))
      printf("holding proc lock\n"); //REMOVE
    panic("sched locks");
  }
  if(t->state == T_RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&t->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct thread *t = mythread();
  acquire(&t->lock);
  t->state = T_RUNNABLE;
  sched();
  release(&t->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  static int first = 1;

  // Still holding p->lock and t->lock from scheduler.
  release(&mythread()->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    first = 0;
    fsinit(ROOTDEV);
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct thread *t = mythread();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&t->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  t->chan = chan;
  t->state = T_SLEEPING;

  sched();

  // Tidy up.
  t->chan = 0;

  // Reacquire original lock.
  release(&t->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
  struct thread *t;
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    for(t = p->p_threads; t < &p->p_threads[NTHREAD]; t++) {
      if(t != mythread()){
        acquire(&t->lock);
        if(t->state == T_SLEEPING && t->chan == chan) {
          t->state = T_RUNNABLE;
        }
        release(&t->lock);
      }
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid, int signum)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      // 2.2.1 Updating the kill system call
      if(p->state == ZOMBIE || p->state == UNUSED) {
        release(&p->lock);
        return -1;
      }
      p->pending_signals = (p->pending_signals | (1 << signum));

      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  // [SLEEPING]  "sleep ",
  // [RUNNABLE]  "runble",
  // [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}

// 2.1.3 Updating the process signal mask
uint
sigprocmask(uint sigmask)
{
  if (sigmask == SIGKILL || sigmask == SIGSTOP)
    return -1;
  struct proc *p = myproc();
  acquire(&p->lock);
  uint old_sig_mask = p->sig_mask;
  p->sig_mask = sigmask;
  release(&p->lock);
  return old_sig_mask;
}

//2.1.4 Registering Signal Handlers
int 
sigaction(int signum, uint64 act, uint64 oldact)
{
  struct proc *p = myproc();

  // check if signum is legal, act is not null and we're not trying to modify sigkill or sigstop
  if(signum < 0 || signum > NSIGS || signum == SIGKILL || signum == SIGSTOP)
    return -1;
  
  acquire(&p->lock);

  if (oldact) {
    copyout(p->pagetable, oldact, (char*)&p->sig_handlers[signum], sizeof(p->sig_handlers[signum]));
    copyout(p->pagetable, oldact + sizeof(p->sig_handlers[signum]), (char*)&p->sig_handlers_masks[signum], sizeof(uint));
  }

  struct sigaction temp;
  if(act && copyin(p->pagetable, (char*)&temp, act, sizeof(struct sigaction)) >= 0) {
    // check that the new mask is not blocking sigkill or sigstop
    if((((1 << SIGKILL) & temp.sigmask) != 0) || (((1 << SIGSTOP) & temp.sigmask) != 0)) {
      release(&p->lock);
      return -1;
    }

    p->sig_handlers[signum] = temp.sa_handler;
    p->sig_handlers_masks[signum] = temp.sigmask;
  }

  release(&p->lock);
  return 0;
}

// 2.1.5 The sigret system call
void
sigret(void)
{
  struct thread *t = mythread();
  struct proc *p = myproc();

  acquire(&p->lock);
  acquire(&t->lock);
  // Restore the process original trapframe
  memmove(t->trapframe, t->user_trap_backup, sizeof(struct trapframe));
  // Restore the process original signal mask
  p->sig_mask = p->prev_sig_mask;
  // Turn off the flag indicates a user space signal handling for blocking incoming signals at this time.
  p->in_signal_handler = 0;
  release(&t->lock);
  release(&p->lock);
}

// 2.3 Implement kernel space signals
void
sigkill_handler()
{
  struct proc *p = myproc();
  struct thread *t;
  // acquire(&p->lock);
  p->killed = 1;
  for(t = p->p_threads; t < &p->p_threads[NTHREAD]; t++) {
    acquire(&t->lock);
    if(t->state == T_SLEEPING){
      // Wake process from sleep().
      t->state = T_RUNNABLE;
    }
    release(&t->lock);
  }  
  // release(&p->lock);
}

void
sigcont_handler()
{
  struct proc *p = myproc();
  // acquire(&p->lock);

 // If no SIGSTOP in pending signals turn off cont signal bit
  if (((1 << SIGSTOP) & p->pending_signals) == 0)
    p->pending_signals = p->pending_signals & ~(1 << SIGCONT);
  
  // release(&p->lock);
}

void
sigstop_handler()
{
  struct proc *p = myproc();
  
  while(1){
    if (!holding(&p->lock))
      acquire(&p->lock);
    if (!(p->pending_signals & (1 << SIGCONT))){
      release(&p->lock);
      yield();
    } else {
      break;
    }
  }
  p->pending_signals = p->pending_signals & ~(1 << SIGSTOP);
  p->pending_signals = p->pending_signals & ~(1 << SIGCONT);
  // release(&p->lock);
}

// 2.4 Handling Signals
void
handle_signals()
{  
  //TODO: check if we need to signal by priority
  struct proc *p = myproc();
  acquire(&p->lock);
  if (p->in_signal_handler){
    release(&p->lock);
    return;
  }
  for(int i=0; i<NSIGS; i++){
    uint curr_sig = 1 << i;
    if((curr_sig & p->pending_signals) && !(curr_sig & p->sig_mask)){
      if (p->sig_handlers[i] == (void*)SIG_IGN) {
        p->pending_signals = p->pending_signals & ~curr_sig;
        continue;
      }
      if (p->sig_handlers[i] == (void*)SIG_DFL){
        switch(i){
          case SIGKILL:
            sigkill_handler();
            p->pending_signals = p->pending_signals & ~curr_sig;
            break;
          case SIGSTOP:
            sigstop_handler();
            break;
          case SIGCONT:
            sigcont_handler();
            break;
          default:
            sigkill_handler();
            p->pending_signals = p->pending_signals & ~curr_sig;
            break;
        }
      }
      else { 
        struct thread *t = mythread();
        // backup current process mask
        p->prev_sig_mask = p->sig_mask;
        // replace process mask with signal mask
        p->sig_mask = p->sig_handlers_masks[i];
        // disable handling other signals
        p->in_signal_handler = 1;
        // backup user trapframe
        memmove(t->user_trap_backup, t->trapframe, sizeof(struct trapframe));
        // calculate sigret function size
        uint sig_ret_size = sig_ret_end - sig_ret_start;
        // allocate space for sigret function
        t->trapframe->sp -= sig_ret_size;
        // move sigret to stack
        copyout(p->pagetable, (uint64)t->trapframe->sp, (char*)&sig_ret_start, sig_ret_size);
        // move signum to a0
        t->trapframe->a0 = i;
        // move sigret to ra
        t->trapframe->ra = t->trapframe->sp;
        // turn off signal bit
        p->pending_signals = p->pending_signals & ~curr_sig;
        // set pc to signal handler 
        t->trapframe->epc = (uint64)p->sig_handlers[i];
      }
    }
  }
  release(&p->lock);
}

int
kthread_create(uint64 start_func, uint64 stack)
{
  int tid;
  struct proc *p = myproc();
  struct thread* t = mythread();
  struct thread* nt;

  if((nt = allocthread(p)) == 0) {
      return -1;
  }

  tid = nt->tid;

  nt->state = T_RUNNABLE;

  *(nt->trapframe) = *(t->trapframe);

  nt->trapframe->epc = (uint64)start_func;

  nt->trapframe->sp = (uint64)(stack + MAX_STACK_SIZE - 16);

  release(&nt->lock);
  return tid;
}

int
kthread_id() {

  struct thread *t = mythread();

  if(t){
    return t->tid;
  }
  return -1;
  
}

void
kthread_exit(int status)
{
  struct proc *p = myproc();
  struct thread *curr_t = mythread();

  acquire(&p->lock);
  int num_threads_running = 0;
  for (struct thread *t = p->p_threads; t < &p->p_threads[NTHREAD]; t++) {
    acquire(&t->lock);
    if (t->state != T_UNUSED) {
      num_threads_running++;
    }
    release(&t->lock);
  }

  printf("num threads %d\n", num_threads_running);
  //i am last thread running
  if (num_threads_running == 1) {
    release(&p->lock);
    printf("releeasing last thread in kthread_exit\n");
    exit(status);
  }

  acquire(&curr_t->lock);
  curr_t->xstate = status;
  curr_t->state = T_ZOMBIE;

  release(&p->lock);
  
  //wake up threads waiting for this thread to exit
  wakeup(curr_t);
  
  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

int
kthread_join(int thread_id, int *status)
{
  struct thread *t_tojoin  = 0;
  struct proc *p = myproc();

  // find the target thread
  for (struct thread *t = p->p_threads; t < &p->p_threads[NTHREAD]; t++) {
    acquire(&t->lock);
    if (thread_id == t->tid) {
      t_tojoin = t;
      goto found;
    }
    release(&t->lock);
  }

  return -1;

  found:
    //calling thread waits on target thread to finish running
    while (t_tojoin->state != T_ZOMBIE && t_tojoin->state != T_UNUSED) {
      sleep(t_tojoin, &t_tojoin->lock);
    }

    //once target thread is done running, get its status
    if (t_tojoin->state == T_ZOMBIE) {
      if (status != 0 && copyout(p->pagetable, (uint64)status, (char *)&t_tojoin->xstate, sizeof(t_tojoin->xstate)) < 0) {
        release(&t_tojoin->lock);
        return -1;
      }
      freethread(t_tojoin);
    } 

    release(&t_tojoin->lock);
    return 0;
}
