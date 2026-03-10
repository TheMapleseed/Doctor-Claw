#ifndef DOCTORCLAW_MUNINN_H
#define DOCTORCLAW_MUNINN_H

/**
 * Muninn-style cognitive database (C23).
 * Inspired by https://github.com/scrypster/muninndb — engrams with Ebbinghaus-style
 * recency/frequency scoring, Hebbian associations, and Bayesian confidence.
 * Single binary, SQLite-backed, no extra deps.
 */

#include "c23_check.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MUNINN_CONCEPT_MAX 512
#define MUNINN_CONTENT_MAX 16384
#define MUNINN_VAULT_MAX 128
#define MUNINN_ID_MAX 32
#define MUNINN_TAG_MAX 8
#define MUNINN_TAG_LEN 64
#define MUNINN_ACTIVATE_MAX 64
#define MUNINN_DEFAULT_VAULT "default"

typedef enum {
    MUNINN_STATE_ACTIVE,
    MUNINN_STATE_ARCHIVED,
    MUNINN_STATE_SOFT_DELETED,
    MUNINN_STATE_PAUSED,
    MUNINN_STATE_COMPLETED,
    MUNINN_STATE_CANCELLED
} muninn_state_t;

typedef enum {
    MUNINN_TYPE_FACT,
    MUNINN_TYPE_DECISION,
    MUNINN_TYPE_OBSERVATION,
    MUNINN_TYPE_PREFERENCE,
    MUNINN_TYPE_TASK
} muninn_type_t;

typedef struct {
    char id[MUNINN_ID_MAX];
    char vault[MUNINN_VAULT_MAX];
    char concept[MUNINN_CONCEPT_MAX];
    char content[MUNINN_CONTENT_MAX];
    float confidence;           /* 0.0–1.0 Bayesian-style */
    uint32_t access_count;
    uint64_t last_access_sec;   /* Unix time of last retrieval */
    uint64_t created_sec;
    muninn_state_t state;
    muninn_type_t type;
    char tags[MUNINN_TAG_MAX][MUNINN_TAG_LEN];
    size_t tag_count;
} muninn_engram_t;

typedef struct {
    void *db;                   /* sqlite3 * */
    char path[512];
    bool initialized;
} muninn_t;

/**
 * Initialize Muninn DB. Creates DB and tables if missing.
 * path: SQLite path (e.g. workspace_dir/muninn.db).
 */
int muninn_init(muninn_t *m, const char *path);
void muninn_free(muninn_t *m);

/**
 * Write a new engram. concept = short label; content = full text.
 * tags can be NULL; n_tags 0 if no tags. out_id at least MUNINN_ID_MAX.
 * Returns 0 on success, -1 on error.
 */
int muninn_write(muninn_t *m, const char *vault, const char *concept, const char *content,
                 const char *const *tags, size_t n_tags, char *out_id, size_t id_size);

/**
 * Read one engram by id. vault used for namespace.
 */
int muninn_read(muninn_t *m, const char *vault, const char *id, muninn_engram_t *out);

/**
 * ACTIVATE: return up to top_n engrams most relevant to context.
 * Relevance = textual match (FTS) + temporal priority (recency + access_count).
 * Optionally strengthens Hebbian associations between returned engrams.
 * results must hold at least top_n entries; result_count set to actual count.
 */
int muninn_activate(muninn_t *m, const char *vault, const char *context, size_t top_n,
                    muninn_engram_t *results, size_t *result_count);

/**
 * Record that an engram was accessed (e.g. after a read). Updates access_count and last_access.
 * Used internally by activate; exposed for manual recall flows.
 */
int muninn_record_access(muninn_t *m, const char *vault, const char *id);

/**
 * Reinforce (increase) or contradict (decrease) confidence. delta typically in [-0.1, 0.1].
 */
int muninn_reinforce(muninn_t *m, const char *id, float delta);
int muninn_contradict(muninn_t *m, const char *id);

/**
 * Soft-delete an engram (state = SOFT_DELETED). Does not remove row.
 */
int muninn_soft_delete(muninn_t *m, const char *vault, const char *id);

/**
 * List vaults that exist (have at least one engram). vaults[] size at least max_count.
 */
int muninn_list_vaults(muninn_t *m, char vaults[][MUNINN_VAULT_MAX], size_t max_count, size_t *count);

#endif
