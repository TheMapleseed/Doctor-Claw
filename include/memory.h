#ifndef DOCTORCLAW_MEMORY_H
#define DOCTORCLAW_MEMORY_H

#include "c23_check.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#define MAX_MEMORY_KEY 256
#define MAX_MEMORY_VALUE 16384
#define MAX_EMBEDDING_DIM 4096
#define MAX_CHUNK_SIZE 2048
#define MAX_BACKEND_NAME 32

typedef enum {
    MEMORY_BACKEND_SQLITE,
    MEMORY_BACKEND_LUCID,
    MEMORY_BACKEND_MARKDOWN,
    MEMORY_BACKEND_POSTGRES,
    MEMORY_BACKEND_MUNINN,
    MEMORY_BACKEND_NONE,
    MEMORY_BACKEND_UNKNOWN
} memory_backend_t;

typedef enum {
    MEMORY_CATEGORY_SHORT_TERM,
    MEMORY_CATEGORY_LONG_TERM,
    MEMORY_CATEGORY_CORE,
    MEMORY_CATEGORY_WORKING
} memory_category_t;

typedef struct {
    char key[MAX_MEMORY_KEY];
    char value[MAX_MEMORY_VALUE];
    double embedding[MAX_EMBEDDING_DIM];
    size_t embedding_dim;
    uint64_t timestamp;
    char metadata[1024];
    memory_category_t category;
} memory_item_t;

typedef struct {
    memory_backend_t backend;
    void *db_handle;
    char storage_path[512];
    char connection_string[512];
    char name[MAX_BACKEND_NAME];
    bool initialized;
} memory_t;

typedef struct {
    size_t start;
    size_t end;
    char text[MAX_CHUNK_SIZE];
} chunk_t;

typedef struct {
    chunk_t *chunks;
    size_t chunk_count;
    size_t total_chars;
} chunker_result_t;

typedef struct {
    double vector[MAX_EMBEDDING_DIM];
    size_t dim;
} embedding_t;

typedef struct {
    char content[8192];
    size_t tokens;
} vector_search_result_t;

typedef struct {
    char entries[16384];
    size_t count;
} markdown_index_t;

typedef struct {
    char backend_name[MAX_BACKEND_NAME];
    bool selectable;
    memory_backend_t kind;
} memory_backend_info_t;

typedef struct {
    char provider[64];
    char model[64];
    size_t dimensions;
    bool enabled;
} embedding_config_t;

typedef struct {
    bool enabled;
    int ttl_minutes;
    int max_entries;
} cache_config_t;

int memory_init(memory_t *mem, memory_backend_t backend, const char *storage_path);
int memory_store(memory_t *mem, const memory_item_t *item);
int memory_recall(memory_t *mem, const char *key, memory_item_t *out_item);
int memory_search_similar(memory_t *mem, const double *query_embedding, size_t dim, memory_item_t **results, size_t *result_count);
int memory_list(memory_t *mem, memory_item_t **out_items, size_t *out_count);
int memory_delete(memory_t *mem, const char *key);
int memory_clear(memory_t *mem);
void memory_free(memory_t *mem);

int memory_create(const char *backend_name, const char *workspace_dir, memory_t *mem);
memory_backend_t memory_classify(const char *backend_name);
const char* memory_get_backend_name(memory_backend_t backend);
int memory_get_available_backends(memory_backend_info_t *backends, size_t *count);

int memory_chunk_text(const char *text, size_t chunk_size, chunker_result_t *result);
void memory_chunker_free(chunker_result_t *result);

int memory_compute_embedding(const char *text, embedding_t *embedding);
int memory_vector_search(memory_t *mem, const embedding_t *query, size_t top_k, vector_search_result_t *results, size_t *result_count);
int memory_embedder_init(const embedding_config_t *config);
void memory_embedder_free(void);

int memory_hygiene_check(const char *text, char *report, size_t report_size);
int memory_hygiene_run(memory_t *mem, int max_age_days);
int memory_deduplicate(memory_t *mem);

int memory_snapshot_create(memory_t *mem, const char *snapshot_path);
int memory_snapshot_restore(memory_t *mem, const char *snapshot_path);
int memory_snapshot_should_hydrate(const char *workspace_dir);
int memory_snapshot_hydrate(memory_t *mem, const char *workspace_dir);

int memory_lucid_init(memory_t *mem, const char *workspace_dir);
int memory_lucid_store(memory_t *mem, const memory_item_t *item);
int memory_lucid_recall(memory_t *mem, const char *key, memory_item_t *out_item);
int memory_lucid_search(memory_t *mem, const char *query, memory_item_t **results, size_t *count);
void memory_lucid_free(memory_t *mem);

int memory_muninn_init(memory_t *mem, const char *storage_path);
int memory_muninn_store(memory_t *mem, const memory_item_t *item);
int memory_muninn_recall(memory_t *mem, const char *key, memory_item_t *out_item);
void memory_muninn_free(memory_t *mem);

int memory_none_init(memory_t *mem);
int memory_none_store(memory_t *mem, const memory_item_t *item);
int memory_none_recall(memory_t *mem, const char *key, memory_item_t *out_item);
void memory_none_free(memory_t *mem);

int memory_postgres_init(memory_t *mem, const char *connection_string);
int memory_postgres_store(memory_t *mem, const memory_item_t *item);
int memory_postgres_recall(memory_t *mem, const char *query, memory_item_t **results, size_t *count);
void memory_postgres_free(memory_t *mem);

int memory_markdown_index(memory_t *mem, const char *dir_path, markdown_index_t *index);
int memory_markdown_search(memory_t *mem, const char *query, char *results, size_t results_size);

int memory_response_cache_get(const char *prompt, char *response, size_t response_size);
int memory_response_cache_set(const char *prompt, const char *response);
int memory_response_cache_init(const char *workspace_dir, const cache_config_t *config);
void memory_response_cache_free(void);

#endif
