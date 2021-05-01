#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "kernel/syscall.h"
#include "kernel/memlayout.h"
#include "kernel/riscv.h"

struct sigaction {
  void (*sa_handler) (int);
  uint sigmask;
};

void sig_handler_start(){
    return;
}

void sig_handler_1() {
    printf("HELLO IM HANDLER #1\n");
}

void sig_handler_2() {
    printf("HELLO IM HANDLER #2\n");
}

void sig_handler_3() {
    printf("HELLO I AM CHILD\n");
}

int counter = 0;


void sig_handler_4(int signum) {
    counter++;
    printf("counter is: %d by %d with signum %d\n", counter, getpid(), signum);
}

int
test1_check_sigaction_mask_update() {
    int test_status = 0;
    struct sigaction* sig_action_1 = malloc(sizeof(struct sigaction*));
    struct sigaction* sig_action_2 = malloc(sizeof(struct sigaction*));
    sig_action_1->sa_handler = &sig_handler_1;
    sig_action_1->sigmask = 1;

    if(sigaction(2, sig_action_1, 0) == -1){
        printf("test1_check_sigaction_mask_update failed - syscall returned -1\n");
        test_status = 1;
    }

    sigaction(2, 0, sig_action_2);

    if(sig_action_2->sigmask != 1){
        printf("test1_check_sigaction_mask_update failed - sigmask was not updated\n");
        test_status = 1;
    }

    sig_action_1->sa_handler = (void*)SIG_DFL;
    sig_action_1->sigmask = 0;
    for(int i=0; i<NSIGS; i++){
        sigaction(i, sig_action_1, 0);
    }
   return test_status;
}

int
test2_check_sigaction_mask_failure() {
    int test_status = 0;
    struct sigaction* sig_action_1 = malloc(sizeof(struct sigaction*));
    sig_action_1->sigmask = (1 << SIGSTOP);
    if(sigaction(2, sig_action_1, 0) >= 0){
        test_status = 1;
        printf("test2_check_sigaction_mask_failure failed - syscall was supposed to return -1\n");
    }
    sig_action_1->sigmask = (1 << SIGKILL);
    if(sigaction(2, sig_action_1, 0) >= 0){
        test_status = 1;
        printf("test2_check_sigaction_mask_failure failed - syscall was supposed to return -1\n");
    }
    sig_action_1->sigmask = 0;
    if(sigaction(-2, sig_action_1, 0) >= 0){
        test_status = 1;
        printf("test2_check_sigaction_mask_failure failed - syscall was supposed to return -1\n");
    }
    if(sigaction(63, sig_action_1, 0) >= 0){
        test_status = 1;
        printf("test2_check_sigaction_mask_failure failed - syscall was supposed to return -1\n");
    }
    if(sigaction(SIGSTOP, sig_action_1, 0) >= 0){
        printf("test2_check_sigaction_mask_failure failed - syscall was supposed to return -1\n");
    }
    if(sigaction(SIGKILL, sig_action_1, 0) >= 0){
        test_status = 1;
        printf("test2_check_sigaction_mask_failure failed - syscall was supposed to return -1\n");
    }

    sig_action_1->sa_handler = (void*)SIG_DFL;
    sig_action_1->sigmask = 0;
    for(int i=0; i<NSIGS; i++){
        sigaction(i, sig_action_1, 0);
    }
    return test_status;
}

int 
test3_check_sigprocmask() {
    
    int test_status = 0;
    uint first_mask = sigprocmask((1 << 4) | (1 << 3));
    if(first_mask != 0){
        printf("test3_check_sigprocmask failed - first_mask is not previous mask!\n");
        test_status = 1;
    }

    uint second_mask = sigprocmask((1 << 2) | (1 << 3) | (1 << 5));
    if(second_mask != ((1 << 4) | (1 << 3))){
        printf("test3_check_sigprocmask failed - second_mask is not previous mask!\n");
        test_status = 1;
    }

    int pid = fork();
    if(pid == 0){
        uint child_first_mask = sigprocmask((1 << 7) | (1 << 6));
        if(child_first_mask != ((1 << 2) | (1 << 3) | (1 << 5))){
            printf("test3_check_sigprocmask failed - child_first_mask is not previous mask!\n");
            test_status = 1;
        } else {
            char* argv[] = {"tests", "sigprocmask", 0};
            if (exec(argv[0], argv) < 0){
                printf("test3_check_sigprocmask failed - could not exec!\n");
                test_status = 1;
                exit(1);
            }
        }
    }
    int status;
    wait(&status);
    sigprocmask(0);
    return (test_status & status);
}

int test4_check_sigaction_handler_update() {
    int test_status = 0;
    struct sigaction* sig_action_1 = malloc(sizeof(struct sigaction*));
    struct sigaction* sig_action_2 = malloc(sizeof(struct sigaction*));
    sig_action_1->sa_handler = &sig_handler_1;
    sig_action_1->sigmask = 1;
    
    if(sigaction(2, sig_action_1, sig_action_2) < 0){
        printf("test4_check_sigaction_handler_update - first sigaction failed!\n");
        test_status = 1;
    }

    if(sig_action_2->sa_handler != (void*)SIG_DFL){
        printf("test4_check_sigaction_handler_update - sig_action_2 prev handler not DFL!\n");
        test_status = 1;
    }

    if(sigaction(2, 0, sig_action_2) < 0){
        printf("test4_check_sigaction_handler_update - second sigaction failed!\n");
        test_status = 1;
    }

    if(sig_action_2->sa_handler != &sig_handler_1){
        printf("test4_check_sigaction_handler_update - sig_action_2 prev handler not sig_handler_1!\n");
        test_status = 1;
    }

    sig_action_1->sa_handler = &sig_handler_2;
    sigaction(3, sig_action_1, 0);
    sig_action_1->sa_handler = (void*)SIG_IGN;
    sigaction(4, sig_action_1, 0);

    int pid = fork();
    if (pid == 0) {
        struct sigaction* sig_action_3 = malloc(sizeof(struct sigaction*));
        sigaction(2, 0, sig_action_3);
        if (sig_action_3->sa_handler != &sig_handler_1) {
            printf("test4_check_sigaction_handler_update - child did not inherit signal 2 handler!\n");
            test_status = 1;
        }
        sigaction(3, 0, sig_action_3);
        if (sig_action_3->sa_handler != &sig_handler_2) {
            printf("test4_check_sigaction_handler_update - child did not inherit signal 3 handler!\n");
            test_status = 1;
        }
        sigaction(4, 0, sig_action_3);
        if (sig_action_3->sa_handler != (void*)SIG_IGN) {
            printf("test4_check_sigaction_handler_update - child did not inherit signal 4 handler!\n");
            test_status = 1;
        }
        char* argv[] = {"tests", "sigaction_handler", 0};
        if(exec(argv[0],argv)<0){
            printf("test4_check_sigaction_handler_update failed - could not exec!\n");
            test_status = 1;
            exit(1);
        }
    }
    int status;
    wait(&status);
    sig_action_1->sa_handler = (void*)SIG_DFL;
    sig_action_1->sigmask = 0;
    for(int i=0; i<NSIGS; i++){
        sigaction(i, sig_action_1, 0);
    }
    return test_status & status;
}


int test5_check_sending_signals() {
    int test_status = 0;
    int status;
    int pid = fork();
    if (pid == 0) {
        while(1) {
            sleep(10);
        }
    }
    else {
        kill(pid, SIGKILL);
        wait(&status);
    }
    return test_status & status;
}

int test6_check_cont_stop() {
    int test_status = 0;
    struct sigaction* sig_action_1 = malloc(sizeof(struct sigaction*));
    sig_action_1->sa_handler = &sig_handler_3;
    sig_action_1->sigmask = 0;
    if(sigaction(4, sig_action_1, 0) == -1){
        printf("sigaction failed\n");
    }
    
    int pid = fork();

    if(pid == 0){
        while(1){}
    }

    sleep(5);
    kill(pid, SIGSTOP);
    sleep(10);
    kill(pid, 4);
    sleep(10);
    kill(pid, SIGCONT);
    sleep(30);
    kill(pid, SIGKILL);
    int status;
    wait(&status);

    return test_status & status;
}

int test7_spawning_multiple_procs(){
    int test_status = 0;
    struct sigaction* sig_action_1 = malloc(sizeof(struct sigaction*));
    sig_action_1->sa_handler = &sig_handler_4;
    sig_action_1->sigmask = 0;

    if(sigaction(2, sig_action_1, 0) == -1)
            printf("sigaction failed\n");
    if(sigaction(3, sig_action_1, 0) == -1)
            printf("sigaction failed\n");
    if(sigaction(4, sig_action_1, 0) == -1)
            printf("sigaction failed\n");
    if(sigaction(5, sig_action_1, 0) == -1)
            printf("sigaction failed\n");
    int curr_pid = getpid();
    for(int i=2; i<6; i++){
        int pid = fork();
        if (pid < 0) {
            printf("fork failed! i is: %d\n", i);
            sleep(10);
            exit(1);
        }
        if(pid == 0){
            kill(curr_pid, i);
            exit(0);
        }
        else {
            sleep(10);
        }
    }
    while(1){
        if(counter == 4){
            break;
        }
    }
    for(int i=2; i<6; i++){
        int status;
        wait(&status);
    }
    
    sig_action_1->sa_handler = (void*)SIG_DFL;
    sig_action_1->sigmask = 0;
    for(int i=0; i<NSIGS; i++){
        sigaction(i, sig_action_1, 0);
    }
    printf("ending\n");
    return test_status;
}

int test7_check_multiple_procs(){ //should print 'hello handler 1' 'hello handler 2' one after the other x 8
    printf("\ninitializing for compiler %p\n", &sig_handler_start);
    int test_status = 0;
    struct sigaction* sig_action_1 = malloc(sizeof(struct sigaction*));
    sig_action_1->sa_handler = &sig_handler_1;
    sig_action_1->sigmask = 0;
    struct sigaction* sig_action_2 = malloc(sizeof(struct sigaction*));
    sig_action_2->sa_handler = &sig_handler_2;
    sig_action_2->sigmask = 0;

    if(sigaction(4, sig_action_1, 0) == -1)
        printf("sigaction failed\n");
    if(sigaction(3, sig_action_2, 0) == -1)
        printf("sigaction failed\n");

    for(int i=0; i<16; i++){
        int pid = fork();
        if(pid == 0){
            if(i%2 == 0){
                kill(getpid(), 4);
            } else if(i%2 == 1) {
                kill(getpid(), 3);
            }
            exit(0);
        } else {
            wait(0);
        }
    }
    return test_status;
}

int test8_check_blocking_signals(){
    int test_status = 0;
    struct sigaction* sig_action_1 = malloc(sizeof(struct sigaction*));
    sig_action_1->sa_handler = &sig_handler_2;
    sig_action_1->sigmask = 0;

    if(sigaction(2, sig_action_1, 0) == -1)
        printf("sigaction failed\n");

    int pid = fork();
    if(pid == 0){
        sigprocmask(4);
        while(1){}
    } else {
        sleep(10);
        kill(pid, 2); //not supposed to print
        sleep(20);
        kill(pid, SIGKILL);
    }

    int status;
    wait(&status);
    return test_status;
}

int
main(int argc, char *argv[]){

    struct test {
    int (*f)();
    char *s;
    } tests[] = {
        {test1_check_sigaction_mask_update, "test1_check_sigaction_mask_update"},
        {test2_check_sigaction_mask_failure, "test2_check_sigaction_mask_failure"},
        {test3_check_sigprocmask, "test3_check_sigprocmask"},
        {test4_check_sigaction_handler_update, "test4_check_sigaction_handler_update"},
        {test5_check_sending_signals, "test5_check_sending_signals"},
        {test6_check_cont_stop, "test6_check_cont_stop"},
       // {test7_spawning_multiple_procs, "test7_spawning_multiple_procs"},
        {test7_check_multiple_procs, "test7_check_multiple_procs"},
        {test8_check_blocking_signals, "test8_check_blocking_signals"},
        {0,0}
    };

    if(argc>1){
        if(strcmp(argv[1],"sigprocmask") == 0){
            uint exec_mask = sigprocmask(0);
            if(exec_mask != ((1 << 7) | (1 << 6))){
                printf("%d", exec_mask);
                printf("test3_check_sigprocmask failed - exec_mask is not previous value!\n");
                exit(1);
            }
            exit(0);
        }
        if(strcmp(argv[1],"sigaction_handler") == 0){
            struct sigaction* sig_action_3 = malloc(sizeof(struct sigaction*));
            sigaction(2, 0, sig_action_3);
            if (sig_action_3->sa_handler != (void*)SIG_DFL) {
                printf("test4_check_sigaction_handler_update - exec did not initialize signal 2 handler!\n");
                exit(1);
            }
            sigaction(3, 0, sig_action_3);
            if (sig_action_3->sa_handler != (void*)SIG_DFL) {
                printf("test4_check_sigaction_handler_update - exec did not initialize signal 3 handler!\n");
                exit(1);
            }
            sigaction(4, 0, sig_action_3);
            if (sig_action_3->sa_handler != (void*)SIG_IGN) {
                printf("test4_check_sigaction_handler_update - exec did not initialize signal 4 handler!\n");
                exit(1);
            } 
            exit(0);
        }
    }
    for (struct test *t = tests; t->s != 0; t++) {
        printf("%s: ", t->s);
        int test_status = (t->f)();
        if(!test_status) printf("OK\n");
        else printf("FAILED!\n");
    }
    exit(0);  
}

