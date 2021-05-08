#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

#define MAX_BSEM 128

struct binary_semaphore {
    int occupied;
    int value;
    struct spinlock lock;
};

struct binary_semaphore Bsemaphores[MAX_BSEM];

int bsem_alloc() {

    int descriptor;
    int found = 0;
    for (descriptor = 0; !found && descriptor < MAX_BSEM; descriptor++) {
        acquire(&Bsemaphores[descriptor].lock);
        if(!Bsemaphores[descriptor].occupied){
            found = 1;
            break;
        }
    }
    if (!found) {
        release(&Bsemaphores[descriptor].lock);
        return -1;
    }
    Bsemaphores[descriptor].occupied = 1;
    Bsemaphores[descriptor].value = 1; // the allocated semaphore in unlocked state
    release(&Bsemaphores[descriptor].lock);
    return descriptor;
}

void bsem_free(int descriptor) {
    acquire(&Bsemaphores[descriptor].lock);
    Bsemaphores[descriptor].occupied = 0;
    // Bsemaphores[descriptor].value = 1;
    release(&Bsemaphores[descriptor].lock);
}

void bsem_down(int descriptor) {
    acquire(&Bsemaphores[descriptor].lock);
    if (Bsemaphores[descriptor].occupied) {
        while(Bsemaphores[descriptor].value == 0){
            sleep(&Bsemaphores[descriptor], &Bsemaphores[descriptor].lock);
        }
        Bsemaphores[descriptor].value = 0;
    }
    release(&Bsemaphores[descriptor].lock);
}

void bsem_up(int descriptor) {
    acquire(&Bsemaphores[descriptor].lock);
    if (!Bsemaphores[descriptor].occupied) {
        Bsemaphores[descriptor].value = 1;
        wakeup(&Bsemaphores[descriptor]);
    }
    release(&Bsemaphores[descriptor].lock);
}