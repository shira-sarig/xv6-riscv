#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

volatile static int started = 0;

// start() jumps here in supervisor mode on all CPUs.
void
main()
{
  if(cpuid() == 0){ 
    consoleinit();
    printfinit();
    printf("\n");
    printf("xv6 kernel is booting\n");
    printf("\n");
    kinit();         // physical page allocator
    printf("pass1\n");
    kvminit();       // create kernel page table
    printf("pass2\n");
    kvminithart();   // turn on paging
    printf("pass3\n");
    procinit();      // process table
    printf("pass4\n");
    trapinit();      // trap vectors
    printf("pass5\n");
    trapinithart();  // install kernel trap vector
    printf("pass6\n");
    plicinit();      // set up interrupt controller
    printf("pass7\n");
    plicinithart();  // ask PLIC for device interrupts
    printf("pass8\n");
    binit();         // buffer cache
    printf("pass9\n");
    iinit();         // inode cache
    printf("pass10\n");
    fileinit();      // file table
    printf("pass11\n");
    virtio_disk_init(); // emulated hard disk
    printf("pass12\n");
    userinit();      // first user process
    printf("pass13\n");
    __sync_synchronize();
    printf("pass14\n");
    started = 1;
  } else {
    while(started == 0)
      ;
    __sync_synchronize();
    printf("hart %d starting\n", cpuid());
    kvminithart();    // turn on paging
    printf("cpuid: %d, pid: %d, tid: %d , pass1\n", cpuid(), myproc(), mythread());
    trapinithart();   // install kernel trap vector
    printf("cpuid: %d pid: %d, tid: %d , pass2\n", cpuid(), myproc(), mythread());
    plicinithart();   // ask PLIC for device interrupts
    printf("cpuid: %d, pid: %d, tid: %d , pass3\n", cpuid(), myproc(), mythread());
  }

  scheduler();        
}
