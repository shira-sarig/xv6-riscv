#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
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

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(mythread()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;
  int signum;

  if(argint(0, &pid) < 0 || argint(1, &signum) < 0)
    return -1;
  return kill(pid, signum);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// 2.1.3 Updating the process signal mask
uint64
sys_sigprocmask(void)
{
  uint sigmask;

  argaddr(0, (uint64*)&sigmask);
  return sigprocmask(sigmask);
}

//2.1.4 Registering Signal Handlers
uint64
sys_sigaction(void)
{
  int signum;
  uint64 act;
  uint64 old;

  if(argint(0, &signum) < 0)
    return -1;
  argaddr(1, &act);
  argaddr(2, &old);

  return sigaction(signum, act, old);
}

// 2.1.5 The sigret system call
uint64
sys_sigret(void)
{
  sigret();
  return 0;
}


// 3.2 Thread system calls
uint64
sys_kthread_create(void)
{
  uint64 start_func;
  uint64 stack;

  argaddr(0, &start_func);
  argaddr(1, &stack);

  return kthread_create(start_func, stack);
}

uint64
sys_kthread_id(void)
{
  return kthread_id();
}

uint64
sys_kthread_exit(void)
{
  int status;

  if(argint(0, &status) < 0)
    return -1;

  kthread_exit(status);
  return 0;
}

uint64
sys_kthread_join(void)
{
  int thread_id;
  int *status;

  if(argint(0, &thread_id) < 0)
    return -1;

  argaddr(1, (uint64 *)&status);

  return kthread_join(thread_id, status);
}

uint64
sys_bsem_alloc(void)
{
  return bsem_alloc();
}

uint64
sys_bsem_free(void)
{
  int descriptor;

  if(argint(0, &descriptor) < 0)
    return -1;
  
  bsem_free(descriptor);

  return 0;
}

uint64
sys_bsem_down(void)
{
  int descriptor;

  if(argint(0, &descriptor) < 0)
    return -1;
  
  bsem_down(descriptor);

  return 0;
}

uint64
sys_bsem_up(void)
{
  int descriptor;

  if(argint(0, &descriptor) < 0)
    return -1;
  
  bsem_up(descriptor);

  return 0;
}