#ifndef DOCTORCLAW_JOBCACHE_H
#define DOCTORCLAW_JOBCACHE_H

#include "c23_check.h"
#include <stddef.h>
#include <stdbool.h>

/** Job types dispatched by workers; all orchestration flows through the shared cache. */
typedef enum {
    JOB_AGENT_CHAT,
    JOB_CRON_RUN,
    JOB_WEBHOOK,
    JOB_PING
} job_type_t;

/** Called by worker when job completes; for sync HTTP etc. result is NUL-terminated. */
typedef void (*job_response_fn)(void *ctx, const char *result, size_t len, int error);

#define JOB_INSTANCE_ID_LEN 64
#define JOB_PAYLOAD_LEN     8192

typedef struct {
    job_type_t type;
    char instance_id[JOB_INSTANCE_ID_LEN];
    char payload[JOB_PAYLOAD_LEN];
    size_t payload_len;
    job_response_fn response_cb;
    void *response_ctx;
} job_t;

/** Opaque shared job cache (thread-safe queue). */
typedef struct jobcache jobcache_t;

/** Create cache with max capacity. All jobs orchestrated through this. */
jobcache_t *jobcache_create(size_t max_size);
void jobcache_destroy(jobcache_t *cache);

/** Enqueue; copies job. Returns 0 on success, -1 if full. */
int jobcache_push(jobcache_t *cache, const job_t *job);
/** Dequeue with optional timeout_ms (-1 = block). Returns 0 if job filled, -1 on timeout/empty. */
int jobcache_pop(jobcache_t *cache, job_t *out, int timeout_ms);
/** Current queue depth (load metric for scaling). */
size_t jobcache_depth(jobcache_t *cache);
/** Max capacity. */
size_t jobcache_capacity(jobcache_t *cache);

#endif
