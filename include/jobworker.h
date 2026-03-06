#ifndef DOCTORCLAW_JOBWORKER_H
#define DOCTORCLAW_JOBWORKER_H

#include "c23_check.h"
#include "jobcache.h"
#include "config.h"
#include <stddef.h>

/** Get config for an instance (for task-based execution). NULL = use default. */
typedef int (*jobworker_config_fn)(const char *instance_id, config_t *config_out);

/** Opaque worker pool: tasks pulled from shared cache, load-based scaling. */
typedef struct jobworker_pool jobworker_pool_t;

/**
 * Create worker pool. All jobs orchestrated through cache; instances are task-based.
 * get_config: resolve instance_id -> config (can be NULL: then single config used for all).
 * default_config: used when get_config is NULL or get_config returns non-zero.
 */
jobworker_pool_t *jobworker_pool_create(
    jobcache_t *cache,
    jobworker_config_fn get_config,
    const config_t *default_config,
    size_t min_workers,
    size_t max_workers
);

void jobworker_pool_destroy(jobworker_pool_t *pool);

/** Start workers and load-based scaler. */
int jobworker_pool_start(jobworker_pool_t *pool);
/** Stop all workers and scaler. */
void jobworker_pool_stop(jobworker_pool_t *pool);

/** High-water mark: scale up when queue depth exceeds this. */
void jobworker_pool_set_scale_up_threshold(jobworker_pool_t *pool, size_t depth);
/** Scale down when depth has been 0 for this many scaler cycles. */
void jobworker_pool_set_scale_down_idle_cycles(jobworker_pool_t *pool, unsigned int cycles);

#endif
