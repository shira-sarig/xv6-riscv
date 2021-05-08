#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define print(s) printf("%s\n", s);
#define STACK_SIZE 4000
#define NTHREADS 2
int shared = 0;
void func()
{
    sleep(1);
    shared++;
    kthread_exit(7);
}

int main(int argc, char *argv[])
{
    int tids[NTHREADS];
    void *stacks[NTHREADS];
    for (int i = 0; i < NTHREADS; i++)
    {
        void *stack = malloc(STACK_SIZE);
        tids[i] = kthread_create(func, stack);
        stacks[i] = stack;
    }

    for (int i = 0; i < NTHREADS; i++)
    {
        int status;
        kthread_join(tids[i], &status);
        free(stacks[i]);
        printf("the status is: %d\n", status);
    }
    printf("%d\n", shared);
    exit(0);
}