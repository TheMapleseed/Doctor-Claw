#include "jobcache.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>

struct jobcache {
    job_t *slots;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;
    pthread_mutex_t mutex;
    pthread_cond_t cond_not_empty;
    pthread_cond_t cond_not_full;
};

jobcache_t *jobcache_create(size_t max_size) {
    if (max_size == 0) return NULL;
    jobcache_t *cache = (jobcache_t *)malloc(sizeof(jobcache_t));
    if (!cache) return NULL;
    cache->slots = (job_t *)malloc(sizeof(job_t) * max_size);
    if (!cache->slots) {
        free(cache);
        return NULL;
    }
    cache->capacity = max_size;
    cache->head = 0;
    cache->tail = 0;
    cache->count = 0;
    pthread_mutex_init(&cache->mutex, NULL);
    pthread_cond_init(&cache->cond_not_empty, NULL);
    pthread_cond_init(&cache->cond_not_full, NULL);
    return cache;
}

void jobcache_destroy(jobcache_t *cache) {
    if (!cache) return;
    pthread_mutex_destroy(&cache->mutex);
    pthread_cond_destroy(&cache->cond_not_empty);
    pthread_cond_destroy(&cache->cond_not_full);
    free(cache->slots);
    free(cache);
}

int jobcache_push(jobcache_t *cache, const job_t *job) {
    if (!cache || !job) return -1;
    pthread_mutex_lock(&cache->mutex);
    while (cache->count >= cache->capacity)
        pthread_cond_wait(&cache->cond_not_full, &cache->mutex);
    job_t *slot = &cache->slots[cache->tail];
    memcpy(slot, job, sizeof(job_t));
    if (job->payload_len < sizeof(slot->payload))
        slot->payload[job->payload_len] = '\0';
    cache->tail = (cache->tail + 1) % cache->capacity;
    cache->count++;
    pthread_cond_signal(&cache->cond_not_empty);
    pthread_mutex_unlock(&cache->mutex);
    return 0;
}

int jobcache_pop(jobcache_t *cache, job_t *out, int timeout_ms) {
    if (!cache || !out) return -1;
    pthread_mutex_lock(&cache->mutex);
    if (timeout_ms < 0) {
        while (cache->count == 0)
            pthread_cond_wait(&cache->cond_not_empty, &cache->mutex);
    } else if (timeout_ms > 0) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += timeout_ms / 1000;
        ts.tv_nsec += (timeout_ms % 1000) * 1000000;
        if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
        while (cache->count == 0) {
            int r = pthread_cond_timedwait(&cache->cond_not_empty, &cache->mutex, &ts);
            if (r != 0) {
                pthread_mutex_unlock(&cache->mutex);
                return -1;
            }
        }
    } else {
        if (cache->count == 0) {
            pthread_mutex_unlock(&cache->mutex);
            return -1;
        }
    }
    memcpy(out, &cache->slots[cache->head], sizeof(job_t));
    cache->head = (cache->head + 1) % cache->capacity;
    cache->count--;
    pthread_cond_signal(&cache->cond_not_full);
    pthread_mutex_unlock(&cache->mutex);
    return 0;
}

size_t jobcache_depth(jobcache_t *cache) {
    if (!cache) return 0;
    pthread_mutex_lock(&cache->mutex);
    size_t n = cache->count;
    pthread_mutex_unlock(&cache->mutex);
    return n;
}

size_t jobcache_capacity(jobcache_t *cache) {
    return cache ? cache->capacity : 0;
}
