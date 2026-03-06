#include "config.h"
#include "cron.h"
#include "jobcache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <pthread.h>
#include <ctype.h>

#define MAX_TASKS 64
#define CRON_DB_PATH ".doctorclaw/cron.db"
#define CRON_STATE_DIR_MAX 512

static jobcache_t *g_cron_jobcache = NULL;
static char g_cron_state_dir[CRON_STATE_DIR_MAX] = {0};

typedef struct {
    char id[64];
    char expression[64];
    char command[512];
    bool enabled;
    time_t last_run;
    time_t next_run;
    int run_count;
    int fail_count;
} cron_task_t;

static cron_task_t g_tasks[MAX_TASKS];
static size_t g_task_count = 0;
static pthread_mutex_t g_cron_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_cron_running __attribute__((unused)) = 0;
static pthread_t g_cron_thread __attribute__((unused));

static __attribute__((unused)) int parse_cron_expression(const char *expression, time_t *next_run);
static time_t calculate_next_run(const char *expression, time_t from_time);
static int save_tasks_to_disk(void);
static int load_tasks_from_disk(void);

int cron_init(void) {
    pthread_mutex_lock(&g_cron_mutex);
    
    memset(g_tasks, 0, sizeof(g_tasks));
    g_task_count = 0;
    
    load_tasks_from_disk();
    
    pthread_mutex_unlock(&g_cron_mutex);
    
    printf("[Cron] Scheduler initialized with %zu tasks\n", g_task_count);
    return 0;
}

void cron_set_workspace(const char *state_dir) {
    if (state_dir && state_dir[0]) {
        strncpy(g_cron_state_dir, state_dir, CRON_STATE_DIR_MAX - 1);
        g_cron_state_dir[CRON_STATE_DIR_MAX - 1] = '\0';
    } else {
        g_cron_state_dir[0] = '\0';
    }
}

static int save_tasks_to_disk(void) {
    char db_path[512] = {0};
    char dir_path[512] = {0};
    if (g_cron_state_dir[0]) {
        snprintf(dir_path, sizeof(dir_path), "%s", g_cron_state_dir);
        mkdir(dir_path, 0755);
        snprintf(db_path, sizeof(db_path), "%s/cron.db", g_cron_state_dir);
    } else {
        const char *home = getenv("HOME") ? getenv("HOME") : ".";
        snprintf(dir_path, sizeof(dir_path), "%s/.doctorclaw", home);
        mkdir(dir_path, 0755);
        snprintf(db_path, sizeof(db_path), "%s/%s", home, CRON_DB_PATH);
    }
    
    FILE *f = fopen(db_path, "w");
    if (!f) return -1;
    
    fprintf(f, "# Doctor Claw Cron Tasks\n");
    fprintf(f, "# Format: id|expression|command|enabled|last_run|next_run|run_count|fail_count\n");
    
    for (size_t i = 0; i < g_task_count; i++) {
        cron_task_t *task = &g_tasks[i];
        fprintf(f, "%s|%s|%s|%d|%ld|%ld|%d|%d\n",
                task->id,
                task->expression,
                task->command,
                task->enabled ? 1 : 0,
                (long)task->last_run,
                (long)task->next_run,
                task->run_count,
                task->fail_count);
    }
    
    fclose(f);
    return 0;
}

static int load_tasks_from_disk(void) {
    char db_path[512] = {0};
    if (g_cron_state_dir[0])
        snprintf(db_path, sizeof(db_path), "%s/cron.db", g_cron_state_dir);
    else
        snprintf(db_path, sizeof(db_path), "%s/%s", getenv("HOME") ? getenv("HOME") : ".", CRON_DB_PATH);
    FILE *f = fopen(db_path, "r");
    if (!f) return 0;
    
    char line[1024];
    while (fgets(line, sizeof(line), f) && g_task_count < MAX_TASKS) {
        if (line[0] == '#' || line[0] == '\n') continue;
        
        cron_task_t *task = &g_tasks[g_task_count];
        
        char *id = strtok(line, "|");
        char *expr = strtok(NULL, "|");
        char *cmd = strtok(NULL, "|");
        char *enabled = strtok(NULL, "|");
        char *last_run = strtok(NULL, "|");
        char *next_run = strtok(NULL, "|");
        char *run_count = strtok(NULL, "|");
        char *fail_count = strtok(NULL, "|\n");
        
        if (id && expr && cmd) {
            snprintf(task->id, sizeof(task->id), "%s", id);
            snprintf(task->expression, sizeof(task->expression), "%s", expr);
            snprintf(task->command, sizeof(task->command), "%s", cmd);
            task->enabled = (enabled && atoi(enabled) == 1);
            task->last_run = last_run ? atol(last_run) : 0;
            task->next_run = next_run ? atol(next_run) : time(NULL) + 60;
            task->run_count = run_count ? atoi(run_count) : 0;
            task->fail_count = fail_count ? atoi(fail_count) : 0;
            
            if (task->next_run < time(NULL)) {
                task->next_run = calculate_next_run(task->expression, time(NULL));
            }
            
            g_task_count++;
        }
    }
    
    fclose(f);
    return 0;
}

static time_t calculate_next_run(const char *expression, time_t from_time) {
    struct tm *tm = localtime(&from_time);
    (void)tm;
    char fields[6][32] = {0};
    int field_idx = 0;
    char *expr_copy = strdup(expression);
    char *token = strtok(expr_copy, " \t");
    
    while (token && field_idx < 6) {
        snprintf(fields[field_idx], sizeof(fields[field_idx]), "%s", token);
        token = strtok(NULL, " \t");
        field_idx++;
    }
    free(expr_copy);
    
    if (field_idx < 5) {
        return from_time + 60;
    }
    
    int interval = 60;
    
    if (strcmp(fields[0], "*") != 0) {
        int val = atoi(fields[0]);
        if (val >= 0 && val <= 59) interval = 60;
    }
    if (strcmp(fields[1], "*") != 0) {
        int val = atoi(fields[1]);
        if (val >= 0 && val <= 23) interval = 3600;
    }
    
    time_t next = ((from_time / interval) + 1) * interval;
    
    return next;
}

int cron_add_task(const char *id, const char *expression, const char *command) {
    pthread_mutex_lock(&g_cron_mutex);
    
    if (g_task_count >= MAX_TASKS) {
        pthread_mutex_unlock(&g_cron_mutex);
        return -1;
    }
    
    for (size_t i = 0; i < g_task_count; i++) {
        if (strcmp(g_tasks[i].id, id) == 0) {
            pthread_mutex_unlock(&g_cron_mutex);
            return -1;
        }
    }
    
    cron_task_t *task = &g_tasks[g_task_count++];
    snprintf(task->id, sizeof(task->id), "%s", id);
    snprintf(task->expression, sizeof(task->expression), "%s", expression);
    snprintf(task->command, sizeof(task->command), "%s", command);
    task->enabled = true;
    task->last_run = 0;
    task->next_run = calculate_next_run(expression, time(NULL));
    task->run_count = 0;
    task->fail_count = 0;
    
    save_tasks_to_disk();
    
    pthread_mutex_unlock(&g_cron_mutex);
    
    printf("[Cron] Added task: %s (%s) -> %s\n", id, expression, command);
    return 0;
}

int cron_remove_task(const char *id) {
    pthread_mutex_lock(&g_cron_mutex);
    
    for (size_t i = 0; i < g_task_count; i++) {
        if (strcmp(g_tasks[i].id, id) == 0) {
            for (size_t j = i; j < g_task_count - 1; j++) {
                g_tasks[j] = g_tasks[j + 1];
            }
            g_task_count--;
            save_tasks_to_disk();
            pthread_mutex_unlock(&g_cron_mutex);
            printf("[Cron] Removed task: %s\n", id);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_cron_mutex);
    return -1;
}

int cron_list_tasks(void) {
    pthread_mutex_lock(&g_cron_mutex);
    
    printf("Cron Tasks (%zu):\n", g_task_count);
    for (size_t i = 0; i < g_task_count; i++) {
        cron_task_t *task = &g_tasks[i];
        char timebuf[64] = {0};
        
        if (task->next_run > 0) {
            struct tm *tm = localtime(&task->next_run);
            strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M", tm);
        }
        
        printf("  [%s] %s -> %s (enabled=%s, next=%s, runs=%d, fails=%d)\n",
               task->id,
               task->expression,
               task->command,
               task->enabled ? "yes" : "no",
               timebuf,
               task->run_count,
               task->fail_count);
    }
    
    pthread_mutex_unlock(&g_cron_mutex);
    return 0;
}

void cron_set_jobcache(jobcache_t *cache) {
    g_cron_jobcache = cache;
}

int cron_run_pending(void) {
    pthread_mutex_lock(&g_cron_mutex);
    
    time_t now = time(NULL);
    
    for (size_t i = 0; i < g_task_count; i++) {
        cron_task_t *task = &g_tasks[i];
        
        if (!task->enabled) continue;
        if (now < task->next_run) continue;
        
        if (g_cron_jobcache) {
            job_t job = {0};
            job.type = JOB_CRON_RUN;
            snprintf(job.instance_id, sizeof(job.instance_id), "default");
            size_t cmd_len = strlen(task->command);
            if (cmd_len >= sizeof(job.payload)) cmd_len = sizeof(job.payload) - 1;
            memcpy(job.payload, task->command, cmd_len);
            job.payload[cmd_len] = '\0';
            job.payload_len = cmd_len;
            if (jobcache_push(g_cron_jobcache, &job) == 0) {
                printf("[Cron] Enqueued task %s to shared cache\n", task->id);
                task->last_run = now;
                task->next_run = calculate_next_run(task->expression, now);
                task->run_count++;
            }
        } else {
            printf("[Cron] Running task %s: %s\n", task->id, task->command);
            int result = system(task->command);
            task->last_run = now;
            task->next_run = calculate_next_run(task->expression, now);
            if (result == 0) {
                task->run_count++;
            } else {
                task->fail_count++;
            }
        }
    }
    
    save_tasks_to_disk();
    
    pthread_mutex_unlock(&g_cron_mutex);
    
    return 0;
}

int cron_enable_task(const char *id, int enabled) {
    pthread_mutex_lock(&g_cron_mutex);
    
    for (size_t i = 0; i < g_task_count; i++) {
        if (strcmp(g_tasks[i].id, id) == 0) {
            g_tasks[i].enabled = (enabled != 0);
            if (g_tasks[i].enabled) {
                g_tasks[i].next_run = calculate_next_run(g_tasks[i].expression, time(NULL));
            }
            save_tasks_to_disk();
            pthread_mutex_unlock(&g_cron_mutex);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_cron_mutex);
    return -1;
}

int cron_run_task_now(const char *id) {
    pthread_mutex_lock(&g_cron_mutex);
    
    for (size_t i = 0; i < g_task_count; i++) {
        if (strcmp(g_tasks[i].id, id) == 0) {
            printf("[Cron] Running task NOW: %s\n", g_tasks[i].command);
            
            int result = system(g_tasks[i].command);
            g_tasks[i].last_run = time(NULL);
            
            if (result == 0) {
                g_tasks[i].run_count++;
            } else {
                g_tasks[i].fail_count++;
            }
            
            g_tasks[i].next_run = calculate_next_run(g_tasks[i].expression, time(NULL));
            save_tasks_to_disk();
            
            pthread_mutex_unlock(&g_cron_mutex);
            return result;
        }
    }
    
    pthread_mutex_unlock(&g_cron_mutex);
    return -1;
}

void cron_shutdown(void) {
    pthread_mutex_lock(&g_cron_mutex);
    save_tasks_to_disk();
    g_task_count = 0;
    pthread_mutex_unlock(&g_cron_mutex);
    printf("[Cron] Scheduler shut down\n");
}
