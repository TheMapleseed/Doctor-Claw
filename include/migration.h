#ifndef DOCTORCLAW_MIGRATION_H
#define DOCTORCLAW_MIGRATION_H

#include "c23_check.h"
#include <stdbool.h>
#include <stddef.h>

#define MAX_MIGRATION_NAME 64
#define MAX_MIGRATION_PATH 512

typedef enum {
    MIGRATION_SOURCE_CLAUDE,
    MIGRATION_SOURCE_OPENAI,
    MIGRATION_SOURCE_OLLAMA,
    MIGRATION_SOURCE_LLAMACPP,
    MIGRATION_SOURCE_GENERIC,
    MIGRATION_SOURCE_NONE
} migration_source_t;

typedef enum {
    MIGRATION_STATE_PENDING,
    MIGRATION_STATE_SCANNING,
    MIGRATION_STATE_MIGRATING,
    MIGRATION_STATE_COMPLETED,
    MIGRATION_STATE_FAILED
} migration_state_t;

typedef struct {
    char key[256];
    char value[16384];
    char source[64];
} migration_item_t;

typedef struct {
    migration_source_t source;
    migration_state_t state;
    char source_path[MAX_MIGRATION_PATH];
    char dest_path[MAX_MIGRATION_PATH];
    size_t items_scanned;
    size_t items_migrated;
    size_t items_failed;
    char error[512];
} migration_t;

typedef struct {
    migration_t migrations[16];
    size_t migration_count;
} migration_manager_t;

int migration_manager_init(migration_manager_t *mgr);
int migration_add(migration_manager_t *mgr, migration_source_t source, const char *source_path, const char *dest_path);
int migration_execute(migration_manager_t *mgr, const char *name);
int migration_status(migration_manager_t *mgr, const char *name, migration_t *out_migration);
int migration_list(migration_manager_t *mgr, migration_t **out_migrations, size_t *out_count);
int migration_rollback(migration_manager_t *mgr, const char *name);
void migration_manager_free(migration_manager_t *mgr);

const char *migration_source_name(migration_source_t source);
migration_source_t migration_source_from_name(const char *name);
const char *migration_state_name(migration_state_t state);

#endif
