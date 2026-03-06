#ifndef DOCTORCLAW_CRON_H
#define DOCTORCLAW_CRON_H

#include "c23_check.h"

/** Opaque per-instance cron context for single-process multi-instance mode. */
typedef struct cron_ctx cron_ctx_t;

int cron_init(void);
/** Set state dir for default context (used by cron_init when no ctx). */
void cron_set_workspace(const char *state_dir);
int cron_run_pending(void);
int cron_add_task(const char *id, const char *expression, const char *command);
int cron_remove_task(const char *id);
/** List tasks to stdout (call after cron_init). */
int cron_list_tasks(void);
void cron_shutdown(void);

/** Per-instance API: create/destroy context, run pending for that instance. */
cron_ctx_t *cron_create(const char *state_dir);
int cron_run_pending_ctx(cron_ctx_t *ctx);
void cron_destroy(cron_ctx_t *ctx);

/** When set, cron_run_pending enqueues due tasks to the shared job cache instead of running them. */
typedef struct jobcache jobcache_t;
void cron_set_jobcache(jobcache_t *cache);

#endif
