struct stat;
struct rtcdate;
struct sigaction;

// system calls
int fork(void);
int exit(int) __attribute__((noreturn));
int wait(int*);
int pipe(int*);
int write(int, const void*, int);
int read(int, void*, int);
int close(int);
int kill(int, int);
int exec(char*, char**);
int open(const char*, int);
int mknod(const char*, short, short);
int unlink(const char*);
int fstat(int fd, struct stat*);
int link(const char*, const char*);
int mkdir(const char*);
int chdir(const char*);
int dup(int);
int getpid(void);
char* sbrk(int);
int sleep(int);
int uptime(void);

// 2.1.3 Updating the process signal mask
int sigprocmask(uint);
//2.1.4 Registering Signal Handlers
int sigaction (int, const struct sigaction*, struct sigaction*);
// 2.1.5 The sigret system call
void sigret(void);
// 3. Threads
int kthread_create (void (*)(), void *);
int kthread_id(void);
void kthread_exit(int);
int kthread_join(int, int*);
// 4.a. binary semaphore
int  bsem_alloc(void);
void bsem_free(int);
void bsem_down(int);
void bsem_up(int);

// ulib.c
int stat(const char*, struct stat*);
char* strcpy(char*, const char*);
void *memmove(void*, const void*, int);
char* strchr(const char*, char c);
int strcmp(const char*, const char*);
void fprintf(int, const char*, ...);
void printf(const char*, ...);
char* gets(char*, int max);
uint strlen(const char*);
void* memset(void*, int, uint);
void* malloc(uint);
void free(void*);
int atoi(const char*);
int memcmp(const void *, const void *, uint);
void *memcpy(void *, const void *, uint);
