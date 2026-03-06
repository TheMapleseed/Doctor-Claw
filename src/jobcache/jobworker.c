#include "jobworker.h"
#include "agent.h"
#include "tools.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

#define POP_TIMEOUT_MS   1000
#define SCALER_INTERVAL_SEC 5
#define DEFAULT_SCALE_UP_DEPTH 8

struct jobworker_pool {
    jobcache_t *cache;
    jobworker_config_fn get_config;
    config_t default_config;
    size_t min_workers;
    size_t max_workers;
    size_t scale_up_threshold;
    unsigned int scale_down_idle_cycles;
    size_t num_workers;
    int running;
    pthread_t *worker_threads;
    pthread_t scaler_thread;
    pthread_mutex_t mutex;
};

static void *worker_thread_fn(void *arg);
static void *scaler_thread_fn(void *arg);
static void dispatch_job(const job_t *job, config_t *config, char *result_buf, size_t result_size, int *error_out);

jobworker_pool_t *jobworker_pool_create(
    jobcache_t *cache,
    jobworker_config_fn get_config,
    const config_t *default_config,
    size_t min_workers,
    size_t max_workers
) {
    if (!cache || !default_config || min_workers == 0 || max_workers < min_workers)
        return NULL;
    jobworker_pool_t *pool = (jobworker_pool_t *)malloc(sizeof(jobworker_pool_t));
    if (!pool) return NULL;
    pool->cache = cache;
    pool->get_config = get_config;
    memcpy(&pool->default_config, default_config, sizeof(config_t));
    pool->min_workers = min_workers;
    pool->max_workers = max_workers;
    pool->scale_up_threshold = DEFAULT_SCALE_UP_DEPTH;
    pool->scale_down_idle_cycles = 3;
    pool->num_workers = 0;
    pool->running = 0;
    pool->worker_threads = NULL;
    pthread_mutex_init(&pool->mutex, NULL);
    return pool;
}

void jobworker_pool_destroy(jobworker_pool_t *pool) {
    if (!pool) return;
    jobworker_pool_stop(pool);
    pthread_mutex_destroy(&pool->mutex);
    free(pool->worker_threads);
    free(pool);
}

static void *worker_thread_fn(void *arg) {
    jobworker_pool_t *pool = (jobworker_pool_t *)arg;
    char result_buf[16384];

    while (pool->running) {
        job_t job;
        int r = jobcache_pop(pool->cache, &job, POP_TIMEOUT_MS);
        if (r != 0) {
            pthread_mutex_lock(&pool->mutex);
            if (!pool->running) {
                pthread_mutex_unlock(&pool->mutex);
                break;
            }
            pthread_mutex_unlock(&pool->mutex);
            continue;
        }

        config_t config;
        if (pool->get_config) {
            if (pool->get_config(job.instance_id[0] ? job.instance_id : "default", &config) != 0)
                memcpy(&config, &pool->default_config, sizeof(config_t));
        } else {
            memcpy(&config, &pool->default_config, sizeof(config_t));
        }

        result_buf[0] = '\0';
        int err = 0;
        dispatch_job(&job, &config, result_buf, sizeof(result_buf), &err);
        if (job.response_cb)
            job.response_cb(job.response_ctx, result_buf, strlen(result_buf), err);
    }
    return NULL;
}

static void dispatch_job(const job_t *job, config_t *config, char *result_buf, size_t result_size, int *error_out) {
    *error_out = 0;
    tools_set_workspace(config->paths.workspace_dir);
    switch (job->type) {
        case JOB_AGENT_CHAT: {
            int task_focus = 0;
            const char *prompt = job->payload;
            if (job->payload_len >= 12 && strncmp(job->payload, "task_focus=1", 12) == 0) {
                task_focus = 1;
                prompt = job->payload + 12;
                while (*prompt == '\n' || *prompt == '\r') prompt++;
            }
            agent_t agent = {0};
            if (agent_init(&agent, config) != 0) {
                snprintf(result_buf, result_size, "Agent init failed");
                *error_out = -1;
                break;
            }
            int chat_r = task_focus
                ? agent_run_task(&agent, prompt, result_buf, result_size)
                : agent_chat(&agent, prompt, result_buf, result_size);
            agent_free(&agent);
            if (chat_r != 0 && result_buf[0] == '\0')
                snprintf(result_buf, result_size, "Agent chat failed");
            if (chat_r != 0) *error_out = -1;
            break;
        }
        case JOB_CRON_RUN: {
            if (job->payload_len > 0) {
                int code = system(job->payload);
                snprintf(result_buf, result_size, "exit=%d", code);
                if (code != 0) *error_out = code;
            }
            break;
        }
        case JOB_WEBHOOK:
            snprintf(result_buf, result_size, "ok");
            break;
        case JOB_PING:
            snprintf(result_buf, result_size, "pong");
            break;
        default:
            snprintf(result_buf, result_size, "unknown job type");
            *error_out = -1;
    }
}

static void *scaler_thread_fn(void *arg) {
    jobworker_pool_t *pool = (jobworker_pool_t *)arg;
    while (pool->running) {
        sleep(SCALER_INTERVAL_SEC);
        if (!pool->running) break;
        size_t depth = jobcache_depth(pool->cache);
        pthread_mutex_lock(&pool->mutex);
        if (depth >= pool->scale_up_threshold && pool->num_workers < pool->max_workers) {
            pthread_t *new_list = (pthread_t *)realloc(
                pool->worker_threads,
                (pool->num_workers + 1) * sizeof(pthread_t)
            );
            if (new_list) {
                pool->worker_threads = new_list;
                if (pthread_create(&pool->worker_threads[pool->num_workers], NULL, worker_thread_fn, pool) == 0)
                    pool->num_workers++;
            }
        }
        pthread_mutex_unlock(&pool->mutex);
    }
    return NULL;
}

int jobworker_pool_start(jobworker_pool_t *pool) {
    if (!pool || pool->running) return -1;
    pool->running = 1;
    pool->worker_threads = (pthread_t *)malloc(pool->min_workers * sizeof(pthread_t));
    if (!pool->worker_threads) return -1;
    pthread_mutex_lock(&pool->mutex);
    for (size_t i = 0; i < pool->min_workers; i++) {
        if (pthread_create(&pool->worker_threads[pool->num_workers], NULL, worker_thread_fn, pool) != 0)
            break;
        pool->num_workers++;
    }
    pthread_mutex_unlock(&pool->mutex);
    if (pthread_create(&pool->scaler_thread, NULL, scaler_thread_fn, pool) != 0) {
        pool->running = 0;
        return -1;
    }
    return 0;
}

void jobworker_pool_stop(jobworker_pool_t *pool) {
    if (!pool || !pool->running) return;
    pool->running = 0;
    pthread_join(pool->scaler_thread, NULL);
    pthread_mutex_lock(&pool->mutex);
    size_t n = pool->num_workers;
    pthread_mutex_unlock(&pool->mutex);
    for (size_t i = 0; i < n; i++)
        pthread_join(pool->worker_threads[i], NULL);
    pool->num_workers = 0;
}

void jobworker_pool_set_scale_up_threshold(jobworker_pool_t *pool, size_t depth) {
    if (pool) pool->scale_up_threshold = depth;
}

void jobworker_pool_set_scale_down_idle_cycles(jobworker_pool_t *pool, unsigned int cycles) {
    if (pool) pool->scale_down_idle_cycles = cycles;
}
