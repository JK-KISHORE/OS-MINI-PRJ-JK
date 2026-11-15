#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_JOBS 64
#define MAX_EVENTS 128
#define LOG_FILE "campuscompute.log"
#define SHM_NAME "/campuscompute_shm_demo"
#define SEM_NAME "/campuscompute_sem_demo"

typedef struct {
    int id, arrival, burst, priority;
    int remaining, start, completion, waiting, turnaround;
} Job;

static void usage(const char *p) {
    printf("CampusCompute - OS mini-project prototype\n"
           "Usage: %s <command> [arguments]\n\n"
           "Commands:\n"
           "  help                         Show this help\n"
           "  schedule <fcfs|rr|priority> [quantum] [--compare]\n"
           "                               Run scheduling on built-in repeatable jobs\n"
           "  logger [items]                Run bounded producer-consumer logger\n"
           "  ipc                           Demonstrate POSIX shared memory + named semaphore\n"
           "  banker <safe|unsafe>          Run Banker's safety algorithm test case\n"
           "  worker <normal|fail>          Fork worker and detect its exit status\n"
           "  limit                         Demonstrate RLIMIT_FSIZE in a child process\n"
           "  demo                          Run all demonstrations in sequence\n"
           "  clean                         Remove this program's log and IPC objects\n\n"
           "Scheduling defaults: Round Robin quantum = 2; priority ties use arrival then job ID.\n", p);
}
static void error_msg(const char *what) { fprintf(stderr, "Error: %s: %s\n", what, strerror(errno)); }

static int load_jobs(Job jobs[]) {
    Job src[] = {
        {1, 0, 5, 2, 0, -1, 0, 0, 0},
        {2, 1, 3, 1, 0, -1, 0, 0, 0},
        {3, 2, 8, 3, 0, -1, 0, 0, 0},
        {4, 3, 6, 2, 0, -1, 0, 0, 0}
    };
    memcpy(jobs, src, sizeof(src));
    return (int)(sizeof(src) / sizeof(src[0]));
}
static void print_jobs(const Job jobs[], int n) {
    double sw=0, st=0;
    printf("\nJob metrics:\n");
    printf("%-6s %-8s %-8s %-9s %-9s %-9s %-9s\n","ID","Arrival","Burst","Priority","Complete","Turnaround","Waiting");
    for (int i=0;i<n;i++) {
        printf("%-6d %-8d %-8d %-9d %-9d %-9d %-9d\n", jobs[i].id,jobs[i].arrival,jobs[i].burst,
               jobs[i].priority,jobs[i].completion,jobs[i].turnaround,jobs[i].waiting);
        sw += jobs[i].waiting; st += jobs[i].turnaround;
    }
    printf("Average waiting time: %.2f\nAverage turnaround time: %.2f\n", sw/n, st/n);
}
static void finish_job(Job *j, int t) {
    j->completion=t; j->turnaround=t-j->arrival; j->waiting=j->turnaround-j->burst;
}
static void schedule_fcfs(Job jobs[], int n) {
    int order[MAX_JOBS], k=0, done[MAX_JOBS]={0}, t=0;
    while(k<n) {
        int pick=-1;
        for(int i=0;i<n;i++) if(!done[i] && jobs[i].arrival<=t &&
            (pick<0 || jobs[i].arrival<jobs[pick].arrival ||
             (jobs[i].arrival==jobs[pick].arrival && jobs[i].id<jobs[pick].id))) pick=i;
        if(pick<0) { t++; continue; }
        if(jobs[pick].start<0) jobs[pick].start=t;
        printf("  [ %d - %d ] J%d\n",t,t+jobs[pick].burst,jobs[pick].id);
        t+=jobs[pick].burst; finish_job(&jobs[pick],t); done[pick]=1; order[k++]=pick;
    }
}
static void schedule_priority(Job jobs[], int n) {
    int done[MAX_JOBS]={0}, completed=0,t=0;
    while(completed<n) {
        int p=-1;
        for(int i=0;i<n;i++) if(!done[i] && jobs[i].arrival<=t &&
            (p<0 || jobs[i].priority<jobs[p].priority ||
             (jobs[i].priority==jobs[p].priority && (jobs[i].arrival<jobs[p].arrival ||
              (jobs[i].arrival==jobs[p].arrival && jobs[i].id<jobs[p].id))))) p=i;
        if(p<0) { t++; continue; }
        jobs[p].start=t; printf("  [ %d - %d ] J%d (priority %d)\n",t,t+jobs[p].burst,jobs[p].id,jobs[p].priority);
        t+=jobs[p].burst; finish_job(&jobs[p],t); done[p]=1; completed++;
    }
}
static void schedule_rr(Job jobs[], int n, int q) {
    int queue[4096], head=0,tail=0,added[MAX_JOBS]={0},completed=0,t=0;
    if(q<=0) { fprintf(stderr,"Quantum must be a positive integer.\n"); return; }
    while(completed<n) {
        for(int i=0;i<n;i++) if(!added[i] && jobs[i].arrival<=t) { queue[tail++]=i; added[i]=1; }
        if(head==tail) { t++; continue; }
        int p=queue[head++], run=jobs[p].remaining<q?jobs[p].remaining:q;
        if(jobs[p].start<0) jobs[p].start=t;
        printf("  [ %d - %d ] J%d\n",t,t+run,jobs[p].id);
        t+=run; jobs[p].remaining-=run;
        for(int i=0;i<n;i++) if(!added[i] && jobs[i].arrival<=t) { queue[tail++]=i; added[i]=1; }
        if(jobs[p].remaining>0) queue[tail++]=p;
        else { finish_job(&jobs[p],t); completed++; }
        if(tail>=4090) { fprintf(stderr,"Internal queue capacity exceeded.\n"); return; }
    }
}
static void run_schedule(const char *policy, int q) {
    Job jobs[MAX_JOBS]; int n=load_jobs(jobs);
    printf("\nScheduling policy: %s",policy);
    if(!strcmp(policy,"rr")) printf(" (time quantum = %d)",q);
    printf("\nTie-break: earlier arrival, then lower job ID; priority uses lower number first.\nTimeline:\n");
    if(!strcmp(policy,"fcfs")) schedule_fcfs(jobs,n);
    else if(!strcmp(policy,"priority")) schedule_priority(jobs,n);
    else if(!strcmp(policy,"rr")) schedule_rr(jobs,n,q);
    else { fprintf(stderr,"Unknown policy '%s'. Use fcfs, rr, or priority.\n",policy); return; }
    print_jobs(jobs,n);
}
static void schedule_command(int argc,char **argv) {
    if(argc<3) { fprintf(stderr,"Usage: %s schedule <fcfs|rr|priority> [quantum] [--compare]\n",argv[0]); return; }
    int q=2, compare=0;
    for(int i=3;i<argc;i++) {
        if(!strcmp(argv[i],"--compare")) compare=1;
        else { char *e; long v=strtol(argv[i],&e,10); if(*e || v<1 || v>1000) { fprintf(stderr,"Invalid quantum.\n"); return; } q=(int)v; }
    }
    if(compare) { run_schedule("fcfs",q); run_schedule("rr",q); run_schedule("priority",q); }
    else run_schedule(argv[2],q);
}

/* Bounded producer-consumer queue. */
typedef struct { char text[160]; } Event;
static Event events[MAX_EVENTS];
static int qhead=0,qtail=0,qcount=0,producer_done=0,logger_items=8;
static pthread_mutex_t qmutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t not_empty=PTHREAD_COND_INITIALIZER,not_full=PTHREAD_COND_INITIALIZER;
static FILE *logfp;
static void log_line(const char *fmt,...) {
    va_list ap; va_start(ap,fmt); vprintf(fmt,ap); va_end(ap);
    va_start(ap,fmt); vfprintf(logfp,fmt,ap); va_end(ap); fflush(logfp);
}
static void *producer(void *arg) {
    (void)arg;
    for(int i=1;i<=logger_items;i++) {
        Event e; snprintf(e.text,sizeof(e.text),"event %d produced by producer",i);
        pthread_mutex_lock(&qmutex);
        while(qcount==MAX_EVENTS) pthread_cond_wait(&not_full,&qmutex);
        events[qtail]=e; qtail=(qtail+1)%MAX_EVENTS; qcount++;
        log_line("[producer] queued %s\n",e.text);
        pthread_cond_signal(&not_empty); pthread_mutex_unlock(&qmutex);
    }
    pthread_mutex_lock(&qmutex); producer_done=1; pthread_cond_broadcast(&not_empty); pthread_mutex_unlock(&qmutex);
    return NULL;
}
static void *consumer(void *arg) {
    (void)arg;
    for(;;) {
        pthread_mutex_lock(&qmutex);
        while(qcount==0 && !producer_done) pthread_cond_wait(&not_empty,&qmutex);
        if(qcount==0 && producer_done) { pthread_mutex_unlock(&qmutex); break; }
        Event e=events[qhead]; qhead=(qhead+1)%MAX_EVENTS; qcount--;
        pthread_cond_signal(&not_full); pthread_mutex_unlock(&qmutex);
        log_line("[consumer] processed %s\n",e.text);
    }
    log_line("[consumer] exiting cleanly\n"); return NULL;
}
static int logger_command(int count) {
    if(count<1 || count>100) { fprintf(stderr,"Items must be between 1 and 100.\n"); return -1; }
    logger_items=count; qhead=qtail=qcount=producer_done=0;
    logfp=fopen(LOG_FILE,"a"); if(!logfp) { error_msg("open log"); return -1; }
    fprintf(logfp,"\n--- logger run (%d items) ---\n",count);
    pthread_t p,c;
    int a=pthread_create(&p,NULL,producer,NULL); if(a) { fprintf(stderr,"pthread_create producer: %s\n",strerror(a)); fclose(logfp); return -1; }
    int b=pthread_create(&c,NULL,consumer,NULL);
    if(b) { fprintf(stderr,"pthread_create consumer: %s\n",strerror(b)); pthread_join(p,NULL); fclose(logfp); return -1; }
    pthread_join(p,NULL); pthread_join(c,NULL);
    pthread_mutex_destroy(&qmutex); pthread_cond_destroy(&not_empty); pthread_cond_destroy(&not_full);
    fclose(logfp); logfp=NULL;
    pthread_mutex_init(&qmutex,NULL); pthread_cond_init(&not_empty,NULL); pthread_cond_init(&not_full,NULL);
    puts("Logger threads joined; log appended to campuscompute.log."); return 0;
}

static void ipc_command(void) {
    int fd=shm_open(SHM_NAME,O_CREAT|O_EXCL|O_RDWR,0600);
    if(fd<0) { error_msg("shm_open (existing demo object? run clean)"); return; }
    if(ftruncate(fd,(off_t)sizeof(int))<0) { error_msg("ftruncate"); close(fd); shm_unlink(SHM_NAME); return; }
    int *shared=mmap(NULL,sizeof(int),PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    if(shared==MAP_FAILED) { error_msg("mmap"); close(fd); shm_unlink(SHM_NAME); return; }
    sem_t *sem=sem_open(SEM_NAME,O_CREAT|O_EXCL,0600,0);
    if(sem==SEM_FAILED) { error_msg("sem_open"); munmap(shared,sizeof(int)); close(fd); shm_unlink(SHM_NAME); return; }
    pid_t pid=fork();
    if(pid<0) { error_msg("fork"); sem_close(sem); sem_unlink(SEM_NAME); munmap(shared,sizeof(int)); close(fd); shm_unlink(SHM_NAME); return; }
    if(pid==0) {
        *shared=2026;
        if(sem_post(sem)<0) _exit(2);
        _exit(0);
    }
    int status; waitpid(pid,&status,0);
    if(sem_wait(sem)<0) error_msg("sem_wait");
    if(WIFEXITED(status) && WEXITSTATUS(status)==0) printf("Parent read shared-memory value: %d\n",*shared);
    else puts("Child process failed during IPC demonstration.");
    sem_close(sem); sem_unlink(SEM_NAME); munmap(shared,sizeof(int)); close(fd); shm_unlink(SHM_NAME);
    puts("Shared memory and named semaphore cleaned up.");
}

static void banker_command(const char *which) {
    int alloc[5][3], max[5][3], avail[3]={3,3,2};
    int safeA[5][3]={{0,1,0},{2,0,0},{3,0,2},{2,1,1},{0,0,2}};
    int safeM[5][3]={{7,5,3},{3,2,2},{9,0,2},{2,2,2},{4,3,3}};
    memcpy(alloc,safeA,sizeof(alloc)); memcpy(max,safeM,sizeof(max));
    if(!strcmp(which,"unsafe")) { avail[0]=0; avail[1]=0; avail[2]=0; }
    else if(strcmp(which,"safe")) { fprintf(stderr,"Choose banker safe or unsafe.\n"); return; }
    int need[5][3], finish[5]={0}, seq[5], k=0;
    for(int i=0;i<5;i++) for(int j=0;j<3;j++) {
        if(max[i][j]<alloc[i][j]) { puts("Invalid state: allocation exceeds maximum."); return; }
        need[i][j]=max[i][j]-alloc[i][j];
    }
    printf("Available: [%d %d %d]\n",avail[0],avail[1],avail[2]);
    puts("Need matrix (Maximum - Allocation):");
    for(int i=0;i<5;i++) printf("P%d: [%d %d %d]\n",i,need[i][0],need[i][1],need[i][2]);
    int work[3]; memcpy(work,avail,sizeof(work));
    int progress=1;
    while(progress) {
        progress=0;
        for(int i=0;i<5;i++) if(!finish[i] && need[i][0]<=work[0] && need[i][1]<=work[1] && need[i][2]<=work[2]) {
            for(int j=0;j<3;j++) work[j]+=alloc[i][j];
            finish[i]=1; seq[k++]=i; progress=1;
        }
    }
    if(k==5) { printf("SAFE state. Safe sequence: "); for(int i=0;i<k;i++) printf("P%d%s",seq[i],i==k-1?"\n":" -> "); }
    else { printf("UNSAFE state. Processes unable to finish: "); for(int i=0;i<5;i++) if(!finish[i]) printf("P%d ",i); puts(""); }
}
static void worker_command(const char *mode) {
    if(strcmp(mode,"normal") && strcmp(mode,"fail")) { fprintf(stderr,"Usage: worker <normal|fail>\n"); return; }
    pid_t p=fork(); if(p<0) { error_msg("fork"); return; }
    if(p==0) { printf("Child worker PID %ld started (%s mode).\n",(long)getpid(),mode); fflush(stdout); _exit(!strcmp(mode,"normal")?0:7); }
    int st; if(waitpid(p,&st,0)<0) { error_msg("waitpid"); return; }
    if(WIFEXITED(st) && WEXITSTATUS(st)==0) printf("Parent: worker %ld completed normally (exit 0).\n",(long)p);
    else if(WIFEXITED(st)) printf("Parent: simulated worker failure detected (exit %d); this is intentional.\n",WEXITSTATUS(st));
    else if(WIFSIGNALED(st)) printf("Parent: worker terminated by signal %d.\n",WTERMSIG(st));
}
static void limit_command(void) {
    pid_t p=fork(); if(p<0) { error_msg("fork"); return; }
    if(p==0) {
        struct rlimit lim={1024,1024};
        if(setrlimit(RLIMIT_FSIZE,&lim)<0) { perror("setrlimit"); _exit(2); }
        signal(SIGXFSZ,SIG_IGN);
        FILE *f=fopen("limit_demo.tmp","w"); if(!f) _exit(3);
        char block[4096]; memset(block,'X',sizeof(block));
        size_t n=fwrite(block,1,sizeof(block),f);
        int flush_result=fflush(f);
        fclose(f);
        printf("Child: RLIMIT_FSIZE soft/hard limit = 1024 bytes; fwrite accepted %zu bytes; fflush result = %d.\n",n,flush_result);
        fflush(stdout);
        _exit(0);
    }
    int st; waitpid(p,&st,0);
    if(WIFEXITED(st)) printf("Parent: limit demo child exit status %d. Check limit_demo.tmp (at most 1024 bytes).\n",WEXITSTATUS(st));
    unlink("limit_demo.tmp");
}
static void clean_command(void) {
    unlink(LOG_FILE);
    if(shm_unlink(SHM_NAME)<0 && errno!=ENOENT) error_msg("shm_unlink");
    if(sem_unlink(SEM_NAME)<0 && errno!=ENOENT) error_msg("sem_unlink");
    unlink("limit_demo.tmp");
    puts("Removed CampusCompute log/temp file and this program's named IPC objects (if present).");
}
int main(int argc,char **argv) {
    if(argc<2 || !strcmp(argv[1],"help") || !strcmp(argv[1],"--help")) { usage(argv[0]); return argc<2?1:0; }
    if(!strcmp(argv[1],"schedule")) { schedule_command(argc,argv); return 0; }
    if(!strcmp(argv[1],"logger")) {
        int n=8; if(argc>2) { char *e; long v=strtol(argv[2],&e,10); if(*e || v<1 || v>100) { fprintf(stderr,"Usage: %s logger [1..100]\n",argv[0]); return 2; } n=(int)v; }
        return logger_command(n)==0?0:1;
    }
    if(!strcmp(argv[1],"ipc")) { ipc_command(); return 0; }
    if(!strcmp(argv[1],"banker")) { if(argc!=3) { fprintf(stderr,"Usage: %s banker <safe|unsafe>\n",argv[0]); return 2; } banker_command(argv[2]); return 0; }
    if(!strcmp(argv[1],"worker")) { if(argc!=3) { fprintf(stderr,"Usage: %s worker <normal|fail>\n",argv[0]); return 2; } worker_command(argv[2]); return 0; }
    if(!strcmp(argv[1],"limit")) { limit_command(); return 0; }
    if(!strcmp(argv[1],"clean")) { clean_command(); return 0; }
    if(!strcmp(argv[1],"demo")) {
        run_schedule("fcfs",2); run_schedule("rr",2); run_schedule("priority",2);
        logger_command(5); ipc_command(); banker_command("safe"); banker_command("unsafe");
        worker_command("normal"); worker_command("fail"); limit_command(); return 0;
    }
    fprintf(stderr,"Unknown command '%s'. Run '%s help'.\n",argv[1],argv[0]); return 2;
}
