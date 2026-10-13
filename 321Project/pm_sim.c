#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#define MAX_PROCESSES  64
#define MAX_CHILDREN   64

typedef enum {
    RUNNING    = 0,
    BLOCKED    = 1,
    ZOMBIE     = 2,
    TERMINATED = 3
} ProcessState;

const char *state_names[] = {"RUNNING", "BLOCKED", "ZOMBIE", "TERMINATED"};

typedef struct {
    int             pid;
    int             ppid;
    ProcessState    state;
    int             exit_status;
    int             children[MAX_CHILDREN];
    int             child_count;
    int             in_use;
    pthread_mutex_t lock;
    pthread_cond_t  cond;
} PCB;

/* ── globals ── */
static PCB   process_table[MAX_PROCESSES];
static int   next_pid = 2;

static pthread_mutex_t table_lock   = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t monitor_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  monitor_cond = PTHREAD_COND_INITIALIZER;

static char  monitor_msg[256];
static int   monitor_pending = 0;
static int   monitor_done = 0;
static FILE *snapshot_file   = NULL;

/* ── helpers (call with table_lock held) ── */
static PCB *find_pcb(int pid)
{
    for (int i = 0; i < MAX_PROCESSES; i++)
        if (process_table[i].in_use && process_table[i].pid == pid)
            return &process_table[i];
    return NULL;
}

static PCB *alloc_slot(void)
{
    for (int i = 0; i < MAX_PROCESSES; i++)
        if (!process_table[i].in_use)
            return &process_table[i];
    return NULL;
}

/* ── print table (call with table_lock held) ── */
static void print_table(FILE *out)
{
    fprintf(out, "PID  PPID  STATE       EXIT_STATUS\n");
    fprintf(out, "----------------------------------------------\n");
    for (int i = 0; i < MAX_PROCESSES; i++) {
        PCB *p = &process_table[i];
        if (!p->in_use || p->state == TERMINATED)
            continue;
        if (p->state == ZOMBIE)
            fprintf(out, "%-5d%-6d%-12s%d\n",
                    p->pid, p->ppid, state_names[p->state], p->exit_status);
        else
            fprintf(out, "%-5d%-6d%-12s-\n",
                    p->pid, p->ppid, state_names[p->state]);
    }
}

/* ── notify monitor - FIX: Now blocks until snapshot is printed ── */
static void notify_monitor(const char *description)
{
    pthread_mutex_lock(&monitor_lock);
    strncpy(monitor_msg, description, sizeof(monitor_msg) - 1);
    monitor_msg[sizeof(monitor_msg) - 1] = '\0';
    monitor_pending = 1;
    pthread_cond_signal(&monitor_cond);
    
    /* Wait until monitor_pending is set to 0 (snapshot printed) */
    while (monitor_pending) {
        pthread_cond_wait(&monitor_cond, &monitor_lock);
    }
    pthread_mutex_unlock(&monitor_lock);
}

/* ── pm_ps ── */
void pm_ps(void)
{
    pthread_mutex_lock(&table_lock);
    print_table(stdout);
    pthread_mutex_unlock(&table_lock);
}

/* ── pm_init: create PID 1 ── */
void pm_init(void)
{
    memset(process_table, 0, sizeof(process_table));

    PCB *init         = &process_table[0];
    init->pid         = 1;
    init->ppid        = 0;
    init->state       = RUNNING;
    init->exit_status = -1;
    init->child_count = 0;
    init->in_use      = 1;
    pthread_mutex_init(&init->lock, NULL);
    pthread_cond_init(&init->cond,  NULL);

    /* console */
    printf("Initial Process Table\n");
    pm_ps();
    printf("\n");

    /* file */
    if (snapshot_file) {
        fprintf(snapshot_file, "Initial Process Table\n");
        pthread_mutex_lock(&table_lock);
        print_table(snapshot_file);
        pthread_mutex_unlock(&table_lock);
        fprintf(snapshot_file, "\n");
        fflush(snapshot_file);
    }
}

/* ── pm_fork ── */
int pm_fork(int parent_pid, int thread_id)
{
    pthread_mutex_lock(&table_lock);

    PCB *parent = find_pcb(parent_pid);
    if (!parent) {
        fprintf(stderr, "[ERROR] pm_fork: parent PID %d not found\n", parent_pid);
        pthread_mutex_unlock(&table_lock);
        return -1;
    }

    PCB *child = alloc_slot();
    if (!child) {
        fprintf(stderr, "[ERROR] pm_fork: process table full\n");
        pthread_mutex_unlock(&table_lock);
        return -1;
    }

    int new_pid        = next_pid++;
    child->pid         = new_pid;
    child->ppid        = parent_pid;
    child->state       = RUNNING;
    child->exit_status = -1;
    child->child_count = 0;
    child->in_use      = 1;
    pthread_mutex_init(&child->lock, NULL);
    pthread_cond_init(&child->cond,  NULL);

    if (parent->child_count < MAX_CHILDREN)
        parent->children[parent->child_count++] = new_pid;

    pthread_mutex_unlock(&table_lock);

    char msg[128];
    snprintf(msg, sizeof(msg), "Thread %d calls pm_fork %d", thread_id, parent_pid);
    notify_monitor(msg);

    return new_pid;
}

/* ── pm_exit ── */
void pm_exit(int pid, int status, int thread_id)
{
    pthread_mutex_lock(&table_lock);

    PCB *proc = find_pcb(pid);
    if (!proc) {
        fprintf(stderr, "[ERROR] pm_exit: PID %d not found\n", pid);
        pthread_mutex_unlock(&table_lock);
        return;
    }

    proc->state       = ZOMBIE;
    proc->exit_status = status;

    /* find parent while we still hold the lock */
    PCB *parent = find_pcb(proc->ppid);
    pthread_mutex_unlock(&table_lock);

    /* 1. print ZOMBIE snapshot FIRST */
    char msg[128];
    snprintf(msg, sizeof(msg), "Thread %d calls pm_exit %d %d", thread_id, pid, status);
    notify_monitor(msg);

    /* 2. THEN wake parent so it reaps AFTER snapshot is printed */
    if (parent) {
        pthread_mutex_lock(&parent->lock);
        pthread_cond_signal(&parent->cond);
        pthread_mutex_unlock(&parent->lock);
    }
}

/* ── pm_wait ── */
void pm_wait(int parent_pid, int child_pid, int thread_id)
{
    PCB *parent = NULL;
    
    pthread_mutex_lock(&table_lock);
    parent = find_pcb(parent_pid);
    if (!parent || parent->child_count == 0) {
        pthread_mutex_unlock(&table_lock);
        return;
    }
    pthread_mutex_unlock(&table_lock);

    pthread_mutex_lock(&parent->lock);

    while (1) {
        pthread_mutex_lock(&table_lock);

        PCB *zombie = NULL;

        if (child_pid == -1) {
            for (int i = 0; i < parent->child_count; i++) {
                PCB *c = find_pcb(parent->children[i]);
                if (c && c->state == ZOMBIE) { zombie = c; break; }
            }
        } else {
            PCB *c = find_pcb(child_pid);
            if (c && c->ppid == parent_pid && c->state == ZOMBIE)
                zombie = c;
        }

        if (zombie) {
            /* reap */
            int reaped_pid = zombie->pid;
            zombie->state  = TERMINATED;
            zombie->in_use = 0;
            for (int i = 0; i < parent->child_count; i++) {
                if (parent->children[i] == reaped_pid) {
                    parent->children[i] = parent->children[--parent->child_count];
                    break;
                }
            }
            parent->state = RUNNING;
            pthread_mutex_unlock(&table_lock);
            break;
        }

        /* no zombie yet — go BLOCKED and sleep */
        parent->state = BLOCKED;
        pthread_mutex_unlock(&table_lock);

        pthread_cond_wait(&parent->cond, &parent->lock);
        /* woken by pm_exit — loop back to check for zombie */
    }

    pthread_mutex_unlock(&parent->lock);

    /* snapshot after reaping (parent back to RUNNING, zombie gone) */
    char msg[128];
    snprintf(msg, sizeof(msg), "Thread %d calls pm_wait %d %d",
             thread_id, parent_pid, child_pid);
    notify_monitor(msg);
}

/* ── pm_kill ── */
void pm_kill(int pid, int thread_id)
{
    pthread_mutex_lock(&table_lock);
    PCB *proc = find_pcb(pid);
    if (!proc) {
        fprintf(stderr, "[ERROR] pm_kill: PID %d not found\n", pid);
        pthread_mutex_unlock(&table_lock);
        return;
    }
    int ppid          = proc->ppid;
    proc->state       = ZOMBIE;
    proc->exit_status = 0;
    PCB *parent = find_pcb(ppid);
    pthread_mutex_unlock(&table_lock);

    char msg[128];
    snprintf(msg, sizeof(msg), "Thread %d calls pm_kill %d", thread_id, pid);
    notify_monitor(msg);

    if (parent) {
        pthread_mutex_lock(&parent->lock);
        pthread_cond_signal(&parent->cond);
        pthread_mutex_unlock(&parent->lock);
    }
}

/* ── monitor thread ── FIX: Atomic message and snapshot printing ── */
void *monitor_func(void *arg)
{
    (void)arg;
    while (1) {
        pthread_mutex_lock(&monitor_lock);
        while (!monitor_pending && !monitor_done)
            pthread_cond_wait(&monitor_cond, &monitor_lock);

        if (monitor_done && !monitor_pending) {
            pthread_mutex_unlock(&monitor_lock);
            break;
        }

        char description[256];
        strncpy(description, monitor_msg, sizeof(description) - 1);
        description[sizeof(description) - 1] = '\0';
        
        /* FIX: DO NOT release monitor_lock yet - keep it locked while printing */
        
        pthread_mutex_unlock(&monitor_lock);

        /* console */
        printf("%s\n", description);
        pthread_mutex_lock(&table_lock);
        print_table(stdout);
        pthread_mutex_unlock(&table_lock);
        printf("\n");
        fflush(stdout);

        /* file */
        if (snapshot_file) {
            fprintf(snapshot_file, "%s\n", description);
            pthread_mutex_lock(&table_lock);
            print_table(snapshot_file);
            pthread_mutex_unlock(&table_lock);
            fprintf(snapshot_file, "\n");
            fflush(snapshot_file);
        }

        /* FIX: Signal waiting thread that snapshot is done */
        pthread_mutex_lock(&monitor_lock);
        monitor_pending = 0;
        pthread_cond_signal(&monitor_cond);
        pthread_mutex_unlock(&monitor_lock);
    }
    return NULL;
}

/* ── worker thread ── */
typedef struct {
    int  thread_id;
    char filename[256];
} WorkerArgs;

void *worker_func(void *arg)
{
    WorkerArgs *wa = (WorkerArgs *)arg;
    int   tid      = wa->thread_id;
    char filename[256];
    
    strncpy(filename, wa->filename, sizeof(filename) - 1);
    filename[sizeof(filename) - 1] = '\0';

    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "[ERROR] Thread %d: cannot open '%s'\n", tid, filename);
        return NULL;
    }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = '\0';
        if (line[0] == '\0' || line[0] == '#')
            continue;

        char cmd[32];
        int  a1 = 0, a2 = 0;
        int  n  = sscanf(line, "%31s %d %d", cmd, &a1, &a2);

        if      (strcmp(cmd, "fork")  == 0 && n >= 2) pm_fork(a1, tid);
        else if (strcmp(cmd, "exit")  == 0 && n >= 3) pm_exit(a1, a2, tid);
        else if (strcmp(cmd, "wait")  == 0 && n >= 3) pm_wait(a1, a2, tid);
        else if (strcmp(cmd, "kill")  == 0 && n >= 2) pm_kill(a1, tid);
        else if (strcmp(cmd, "sleep") == 0 && n >= 2) usleep(a1 * 1000);
        else if (strcmp(cmd, "fork") != 0)
            fprintf(stderr, "[Thread %d] Unknown command: '%s'\n", tid, line);
    }

    fclose(f);
    return NULL;
}

/* ── main ── */
int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s thread0.txt thread1.txt ...\n", argv[0]);
        return 1;
    }

    int num_workers = argc - 1;

    snapshot_file = fopen("snapshots.txt", "w");
    if (!snapshot_file) { perror("fopen snapshots.txt"); return 1; }

    pm_init();

    pthread_t monitor_tid;
    pthread_create(&monitor_tid, NULL, monitor_func, NULL);

    pthread_t  *workers = malloc(sizeof(pthread_t)  * num_workers);
    WorkerArgs *args    = malloc(sizeof(WorkerArgs) * num_workers);

    if (!workers || !args) {
        fprintf(stderr, "[ERROR] Memory allocation failed\n");
        return 1;
    }

    for (int i = 0; i < num_workers; i++) {
        args[i].thread_id = i;
        strncpy(args[i].filename, argv[i + 1], sizeof(args[i].filename) - 1);
        args[i].filename[sizeof(args[i].filename) - 1] = '\0';
        pthread_create(&workers[i], NULL, worker_func, &args[i]);
    }

    for (int i = 0; i < num_workers; i++)
        pthread_join(workers[i], NULL);

    usleep(500000);

    /* Signal monitor thread to exit gracefully */
    pthread_mutex_lock(&monitor_lock);
    monitor_done = 1;
    pthread_cond_signal(&monitor_cond);
    pthread_mutex_unlock(&monitor_lock);

    /* Wait for monitor to actually finish */
    pthread_join(monitor_tid, NULL);

    printf("=== Final Process Table ===\n");
    pm_ps();

    fclose(snapshot_file);
    free(workers);
    free(args);
    return 0;
}