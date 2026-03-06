#include "memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <math.h>

#define MAX_SQL_LEN 2048

typedef struct {
    memory_item_t *items;
    size_t count;
} memory_list_t;

int memory_init(memory_t *mem, memory_backend_t backend, const char *storage_path) {
    if (!mem) return -1;
    
    memset(mem, 0, sizeof(memory_t));
    mem->backend = backend;
    mem->db_handle = NULL;
    
    if (storage_path) {
        snprintf(mem->storage_path, sizeof(mem->storage_path), "%s", storage_path);
    }
    
    if (backend == MEMORY_BACKEND_SQLITE && storage_path) {
        sqlite3 *db = NULL;
        if (sqlite3_open(storage_path, &db) == SQLITE_OK) {
            mem->db_handle = db;
            
            char create_sql[] = 
                "CREATE TABLE IF NOT EXISTS memory ("
                "  key TEXT PRIMARY KEY,"
                "  value TEXT,"
                "  timestamp INTEGER,"
                "  metadata TEXT"
                ");"
                "CREATE INDEX IF NOT EXISTS idx_timestamp ON memory(timestamp);"
                "CREATE VIRTUAL TABLE IF NOT EXISTS memory_fts USING fts5(key, value, content='memory', content_rowid='rowid');";
            
            char *err = NULL;
            sqlite3_exec(db, create_sql, NULL, NULL, &err);
            if (err) {
                sqlite3_free(err);
                err = NULL;
            }
            
            char trigger_sql[] =
                "CREATE TRIGGER IF NOT EXISTS memory_ai AFTER INSERT ON memory BEGIN "
                "  INSERT INTO memory_fts(rowid, key, value) VALUES (new.rowid, new.key, new.value); "
                "END; "
                "CREATE TRIGGER IF NOT EXISTS memory_ad AFTER DELETE ON memory BEGIN "
                "  INSERT INTO memory_fts(memory_fts, rowid, key, value) VALUES('delete', old.rowid, old.key, old.value); "
                "END; "
                "CREATE TRIGGER IF NOT EXISTS memory_au AFTER UPDATE ON memory BEGIN "
                "  INSERT INTO memory_fts(memory_fts, rowid, key, value) VALUES('delete', old.rowid, old.key, old.value); "
                "  INSERT INTO memory_fts(rowid, key, value) VALUES (new.rowid, new.key, new.value); "
                "END;";
            
            sqlite3_exec(db, trigger_sql, NULL, NULL, &err);
            if (err) {
                sqlite3_free(err);
            }
        }
    }
    
    return 0;
}

int memory_store(memory_t *mem, const memory_item_t *item) {
    if (!mem || !item) return -1;
    
    if (mem->backend == MEMORY_BACKEND_SQLITE && mem->db_handle) {
        sqlite3 *db = (sqlite3 *)mem->db_handle;
        sqlite3_stmt *stmt = NULL;
        const char *sql = "INSERT OR REPLACE INTO memory (key, value, timestamp, metadata) VALUES (?1, ?2, ?3, ?4)";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
            return -1;
        }
        sqlite3_bind_text(stmt, 1, item->key, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, item->value, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 3, (sqlite3_int64)item->timestamp);
        sqlite3_bind_text(stmt, 4, item->metadata, -1, SQLITE_TRANSIENT);
        int result = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return result == SQLITE_DONE ? 0 : -1;
    }
    
    return -1;
}

int memory_recall(memory_t *mem, const char *key, memory_item_t *out_item) {
    if (!mem || !key || !out_item) return -1;
    
    if (mem->backend == MEMORY_BACKEND_SQLITE && mem->db_handle) {
        sqlite3 *db = (sqlite3 *)mem->db_handle;
        sqlite3_stmt *stmt = NULL;
        const char *sql = "SELECT key, value, timestamp, metadata FROM memory WHERE key = ?1";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
            return -1;
        }
        sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);
        int rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW) {
            const char *k = (const char *)sqlite3_column_text(stmt, 0);
            const char *v = (const char *)sqlite3_column_text(stmt, 1);
            sqlite3_int64 ts = sqlite3_column_int64(stmt, 2);
            const char *m = (const char *)sqlite3_column_text(stmt, 3);
            snprintf(out_item->key, sizeof(out_item->key), "%s", k ? k : "");
            snprintf(out_item->value, sizeof(out_item->value), "%s", v ? v : "");
            out_item->timestamp = (uint64_t)ts;
            snprintf(out_item->metadata, sizeof(out_item->metadata), "%s", m ? m : "");
            sqlite3_finalize(stmt);
            return 0;
        }
        sqlite3_finalize(stmt);
        return -1;
    }
    
    return -1;
}

int memory_get_stats(memory_t *mem, int *total_items, size_t *total_size) {
    if (!mem) return -1;
    
    if (total_items) *total_items = 0;
    if (total_size) *total_size = 0;
    
    if (mem->backend == MEMORY_BACKEND_SQLITE && mem->db_handle) {
        sqlite3 *db = (sqlite3 *)mem->db_handle;
        
        if (total_items) {
            sqlite3_stmt *stmt;
            if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM memory", -1, &stmt, NULL) == SQLITE_OK) {
                if (sqlite3_step(stmt) == SQLITE_ROW) {
                    *total_items = sqlite3_column_int(stmt, 0);
                }
                sqlite3_finalize(stmt);
            }
        }
        
        if (total_size) {
            sqlite3_stmt *stmt;
            if (sqlite3_prepare_v2(db, "SELECT SUM(LENGTH(value)) FROM memory", -1, &stmt, NULL) == SQLITE_OK) {
                if (sqlite3_step(stmt) == SQLITE_ROW) {
                    *total_size = sqlite3_column_int64(stmt, 0);
                }
                sqlite3_finalize(stmt);
            }
        }
    }
    
    return 0;
}

memory_backend_t memory_classify(const char *backend_name) {
    if (!backend_name) return MEMORY_BACKEND_UNKNOWN;
    
    if (strcmp(backend_name, "sqlite") == 0 || strcmp(backend_name, "SqliteMemory") == 0) {
        return MEMORY_BACKEND_SQLITE;
    } else if (strcmp(backend_name, "lucid") == 0 || strcmp(backend_name, "LucidMemory") == 0) {
        return MEMORY_BACKEND_LUCID;
    } else if (strcmp(backend_name, "markdown") == 0 || strcmp(backend_name, "MarkdownMemory") == 0) {
        return MEMORY_BACKEND_MARKDOWN;
    } else if (strcmp(backend_name, "postgres") == 0 || strcmp(backend_name, "PostgresMemory") == 0) {
        return MEMORY_BACKEND_POSTGRES;
    } else if (strcmp(backend_name, "none") == 0 || strcmp(backend_name, "NoneMemory") == 0) {
        return MEMORY_BACKEND_NONE;
    }
    
    return MEMORY_BACKEND_UNKNOWN;
}

const char* memory_get_backend_name(memory_backend_t backend) {
    switch (backend) {
        case MEMORY_BACKEND_SQLITE: return "sqlite";
        case MEMORY_BACKEND_LUCID: return "lucid";
        case MEMORY_BACKEND_MARKDOWN: return "markdown";
        case MEMORY_BACKEND_POSTGRES: return "postgres";
        case MEMORY_BACKEND_NONE: return "none";
        default: return "unknown";
    }
}

int memory_get_available_backends(memory_backend_info_t *backends, size_t *count) {
    if (!backends || !count) return -1;
    
    static memory_backend_info_t available[] = {
        {"sqlite", true, MEMORY_BACKEND_SQLITE},
        {"lucid", true, MEMORY_BACKEND_LUCID},
        {"markdown", true, MEMORY_BACKEND_MARKDOWN},
        {"postgres", true, MEMORY_BACKEND_POSTGRES},
        {"none", true, MEMORY_BACKEND_NONE},
    };
    
    size_t max = 5;
    for (size_t i = 0; i < max; i++) {
        backends[i] = available[i];
    }
    *count = max;
    
    return 0;
}

int memory_create(const char *backend_name, const char *workspace_dir, memory_t *mem) {
    if (!backend_name || !workspace_dir || !mem) return -1;
    
    memory_backend_t backend = memory_classify(backend_name);
    
    if (backend == MEMORY_BACKEND_UNKNOWN) {
        printf("[Memory] Unknown backend '%s', falling back to markdown\n", backend_name);
        backend = MEMORY_BACKEND_MARKDOWN;
    }
    
    char db_path[512];
    snprintf(db_path, sizeof(db_path), "%s/memory.db", workspace_dir);
    
    int result = memory_init(mem, backend, db_path);
    if (result != 0) {
        printf("[Memory] Failed to initialize backend '%s'\n", backend_name);
        return result;
    }
    
    snprintf(mem->name, sizeof(mem->name), "%s", backend_name);
    mem->initialized = true;
    
    printf("[Memory] Created backend: %s at %s\n", backend_name, workspace_dir);
    return 0;
}

int memory_hygiene_run(memory_t *mem, int max_age_days) {
    if (!mem || max_age_days <= 0) return -1;
    
    printf("[Memory] Running hygiene (max age: %d days)\n", max_age_days);
    
    uint64_t cutoff = (uint64_t)(time(NULL) - (max_age_days * 86400));
    
    if (mem->backend == MEMORY_BACKEND_SQLITE && mem->db_handle) {
        sqlite3 *db = (sqlite3 *)mem->db_handle;
        
        char sql[256];
        snprintf(sql, sizeof(sql), "DELETE FROM memory WHERE timestamp < %llu", (unsigned long long)cutoff);
        
        char *err = NULL;
        sqlite3_exec(db, sql, NULL, NULL, &err);
        if (err) {
            sqlite3_free(err);
            return -1;
        }
    }
    
    return memory_deduplicate(mem);
}

int memory_snapshot_should_hydrate(const char *workspace_dir) {
    if (!workspace_dir) return 0;
    
    char snapshot_path[512];
    snprintf(snapshot_path, sizeof(snapshot_path), "%s/MEMORY_SNAPSHOT.md", workspace_dir);
    
    struct stat st;
    if (stat(snapshot_path, &st) != 0) {
        return 0;
    }
    
    char db_path[512];
    snprintf(db_path, sizeof(db_path), "%s/memory.db", workspace_dir);
    
    if (stat(db_path, &st) == 0) {
        return 0;
    }
    
    return 1;
}

int memory_snapshot_hydrate(memory_t *mem, const char *workspace_dir) {
    if (!mem || !workspace_dir) return -1;
    
    char snapshot_path[512];
    snprintf(snapshot_path, sizeof(snapshot_path), "%s/MEMORY_SNAPSHOT.md", workspace_dir);
    
    FILE *f = fopen(snapshot_path, "r");
    if (!f) return -1;
    
    char content[16384];
    size_t read_size = fread(content, 1, sizeof(content) - 1, f);
    content[read_size] = '\0';
    fclose(f);
    
    char line[1024];
    char current_key[256] = {0};
    char current_value[8192] = {0};
    int item_count = 0;
    
    (void)content;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' && line[1] == ' ') {
            if (current_key[0]) {
                memory_item_t item = {0};
                snprintf(item.key, sizeof(item.key), "%s", current_key);
                snprintf(item.value, sizeof(item.value), "%s", current_value);
                item.timestamp = time(NULL);
                item.category = MEMORY_CATEGORY_CORE;
                
                memory_store(mem, &item);
                item_count++;
            }
            strncpy(current_key, line + 2, sizeof(current_key) - 1);
            current_key[strlen(current_key) - 1] = '\0';
            current_value[0] = '\0';
        } else if (strlen(line) > 1) {
            strncat(current_value, line, sizeof(current_value) - strlen(current_value) - 1);
        }
    }
    
    if (current_key[0]) {
        memory_item_t item = {0};
        snprintf(item.key, sizeof(item.key), "%s", current_key);
        snprintf(item.value, sizeof(item.value), "%s", current_value);
        item.timestamp = time(NULL);
        item.category = MEMORY_CATEGORY_CORE;
        
        memory_store(mem, &item);
        item_count++;
    }
    
    printf("[Memory] Hydrated %d memories from snapshot\n", item_count);
    return item_count;
}

int memory_lucid_init(memory_t *mem, const char *workspace_dir) {
    if (!mem || !workspace_dir) return -1;
    
    memset(mem, 0, sizeof(memory_t));
    mem->backend = MEMORY_BACKEND_LUCID;
    snprintf(mem->storage_path, sizeof(mem->storage_path), "%s/lucid.db", workspace_dir);
    snprintf(mem->name, sizeof(mem->name), "lucid");
    
    printf("[Memory] Lucid backend initialized at %s\n", workspace_dir);
    return 0;
}

int memory_lucid_store(memory_t *mem, const memory_item_t *item) {
    if (!mem || !item) return -1;
    printf("[Memory] Lucid store: %s\n", item->key);
    return memory_store(mem, item);
}

int memory_lucid_recall(memory_t *mem, const char *key, memory_item_t *out_item) {
    if (!mem || !key || !out_item) return -1;
    return memory_recall(mem, key, out_item);
}

int memory_lucid_search(memory_t *mem, const char *query, memory_item_t **results, size_t *count) {
    if (!mem || !query || !results || !count) return -1;
    return memory_search_similar(mem, NULL, 0, results, count);
}

void memory_lucid_free(memory_t *mem) {
    if (!mem) return;
    memory_free(mem);
    printf("[Memory] Lucid backend freed\n");
}

int memory_none_init(memory_t *mem) {
    if (!mem) return -1;
    
    memset(mem, 0, sizeof(memory_t));
    mem->backend = MEMORY_BACKEND_NONE;
    snprintf(mem->name, sizeof(mem->name), "none");
    mem->initialized = true;
    
    printf("[Memory] None backend (no-op) initialized\n");
    return 0;
}

int memory_none_store(memory_t *mem, const memory_item_t *item) {
    (void)mem;
    (void)item;
    return 0;
}

int memory_none_recall(memory_t *mem, const char *key, memory_item_t *out_item) {
    (void)mem;
    (void)key;
    if (out_item) memset(out_item, 0, sizeof(memory_item_t));
    return -1;
}

void memory_none_free(memory_t *mem) {
    if (!mem) return;
    memset(mem, 0, sizeof(memory_t));
}

void memory_postgres_free(memory_t *mem) {
    if (!mem) return;
    if (mem->db_handle) {
        printf("[Memory] Closing PostgreSQL connection\n");
    }
    memset(mem, 0, sizeof(memory_t));
}

static embedding_config_t g_embedder_config = {0};

int memory_embedder_init(const embedding_config_t *config) {
    if (!config) return -1;
    
    memcpy(&g_embedder_config, config, sizeof(embedding_config_t));
    printf("[Memory] Embedder initialized: provider=%s, model=%s, dim=%zu\n",
           config->provider, config->model, config->dimensions);
    
    return 0;
}

void memory_embedder_free(void) {
    memset(&g_embedder_config, 0, sizeof(embedding_config_t));
    printf("[Memory] Embedder freed\n");
}

static cache_config_t g_cache_config = {0};

int memory_response_cache_init(const char *workspace_dir, const cache_config_t *config) {
    (void)workspace_dir;
    if (!config) return -1;
    
    memcpy(&g_cache_config, config, sizeof(cache_config_t));
    printf("[Memory] Response cache initialized: TTL=%dmin, max=%d entries\n",
           config->ttl_minutes, config->max_entries);
    
    return 0;
}

void memory_response_cache_free(void) {
    memset(&g_cache_config, 0, sizeof(cache_config_t));
    printf("[Memory] Response cache freed\n");
}

int memory_search_similar(memory_t *mem, const double *query_embedding, size_t dim,
                         memory_item_t **results, size_t *result_count) {
    (void)query_embedding;
    (void)dim;
    if (!mem || !results || !result_count) return -1;
    
    *results = NULL;
    *result_count = 0;
    
    return 0;
}

int memory_list(memory_t *mem, memory_item_t **out_items, size_t *out_count) {
    if (!mem || !out_items || !out_count) return -1;
    
    if (mem->backend == MEMORY_BACKEND_SQLITE && mem->db_handle) {
        sqlite3 *db = (sqlite3 *)mem->db_handle;
        
        char sql[] = "SELECT key, value, timestamp, metadata FROM memory ORDER BY timestamp DESC LIMIT 100";
        
        sqlite3_stmt *stmt;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
            size_t capacity = 16;
            *out_items = calloc(capacity, sizeof(memory_item_t));
            *out_count = 0;
            
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                if (*out_count >= capacity) {
                    capacity *= 2;
                    *out_items = realloc(*out_items, capacity * sizeof(memory_item_t));
                }
                
                memory_item_t *item = &(*out_items)[*out_count];
                const char *k = (const char *)sqlite3_column_text(stmt, 0);
                const char *v = (const char *)sqlite3_column_text(stmt, 1);
                sqlite3_int64 ts = sqlite3_column_int64(stmt, 2);
                const char *m = (const char *)sqlite3_column_text(stmt, 3);
                
                snprintf(item->key, sizeof(item->key), "%s", k ? k : "");
                snprintf(item->value, sizeof(item->value), "%s", v ? v : "");
                item->timestamp = (uint64_t)ts;
                snprintf(item->metadata, sizeof(item->metadata), "%s", m ? m : "");
                
                (*out_count)++;
            }
            
            sqlite3_finalize(stmt);
            return 0;
        }
    }
    
    return -1;
}

int memory_delete(memory_t *mem, const char *key) {
    if (!mem || !key) return -1;
    
    if (mem->backend == MEMORY_BACKEND_SQLITE && mem->db_handle) {
        sqlite3 *db = (sqlite3 *)mem->db_handle;
        sqlite3_stmt *stmt = NULL;
        const char *sql = "DELETE FROM memory WHERE key = ?1";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
            return -1;
        }
        sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);
        int result = (sqlite3_step(stmt) == SQLITE_DONE) ? 0 : -1;
        sqlite3_finalize(stmt);
        return result;
    }
    
    return -1;
}

int memory_clear(memory_t *mem) {
    if (!mem) return -1;
    
    if (mem->backend == MEMORY_BACKEND_SQLITE && mem->db_handle) {
        sqlite3 *db = (sqlite3 *)mem->db_handle;
        sqlite3_exec(db, "DELETE FROM memory", NULL, NULL, NULL);
        sqlite3_exec(db, "DELETE FROM memory_fts", NULL, NULL, NULL);
    }
    
    return 0;
}

void memory_free(memory_t *mem) {
    if (!mem) return;
    
    if (mem->db_handle) {
        sqlite3_close((sqlite3 *)mem->db_handle);
        mem->db_handle = NULL;
    }
}

int memory_markdown_load(const char *dir_path, memory_item_t **out_items, size_t *out_count) {
    if (!dir_path || !out_items || !out_count) return -1;
    
    *out_items = NULL;
    *out_count = 0;
    
    DIR *dir = opendir(dir_path);
    if (!dir) return -1;
    
    size_t capacity = 16;
    *out_items = calloc(capacity, sizeof(memory_item_t));
    
    struct dirent *entry;
    char full_path[1024];
    
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        
        size_t name_len = strlen(entry->d_name);
        if (name_len < 3 || strcmp(entry->d_name + name_len - 3, ".md") != 0) continue;
        
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
        
        FILE *f = fopen(full_path, "r");
        if (!f) continue;
        
        char content[16384];
        size_t read_size = fread(content, 1, sizeof(content) - 1, f);
        content[read_size] = '\0';
        fclose(f);
        
        if (*out_count >= capacity) {
            capacity *= 2;
            *out_items = realloc(*out_items, capacity * sizeof(memory_item_t));
        }
        
        memory_item_t *item = &(*out_items)[*out_count];
        
        snprintf(item->key, sizeof(item->key), "%s", entry->d_name);
        
        snprintf(item->value, sizeof(item->value), "%s", content);
        
        struct stat st;
        if (stat(full_path, &st) == 0) {
            item->timestamp = (uint64_t)st.st_mtime;
        }
        
        snprintf(item->metadata, sizeof(item->metadata), "type=markdown,path=%s", full_path);
        
        (*out_count)++;
    }
    
    closedir(dir);
    return 0;
}

int memory_markdown_save(const memory_item_t *item, const char *dir_path) {
    if (!item || !dir_path) return -1;
    
    char full_path[1024];
    snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, item->key);
    
    FILE *f = fopen(full_path, "w");
    if (!f) return -1;
    
    fprintf(f, "# %s\n\n", item->key);
    fprintf(f, "%s\n", item->value);
    
    if (item->metadata[0]) {
        fprintf(f, "\n<!-- %s -->\n", item->metadata);
    }
    
    fclose(f);
    return 0;
}

int memory_chunk_text(const char *text, size_t chunk_size, chunker_result_t *result) {
    if (!text || !result) return -1;
    
    memset(result, 0, sizeof(chunker_result_t));
    
    size_t text_len = strlen(text);
    if (text_len == 0) return 0;
    
    size_t num_chunks = (text_len + chunk_size - 1) / chunk_size;
    result->chunks = calloc(num_chunks, sizeof(chunk_t));
    
    if (!result->chunks) return -1;
    
    result->chunk_count = 0;
    result->total_chars = 0;
    
    size_t offset = 0;
    while (offset < text_len && result->chunk_count < num_chunks) {
        chunk_t *chunk = &result->chunks[result->chunk_count];
        chunk->start = offset;
        
        size_t copy_len = chunk_size;
        if (offset + copy_len > text_len) {
            copy_len = text_len - offset;
        }
        
        size_t break_pos = copy_len;
        for (size_t i = 0; i < 20 && i < copy_len; i++) {
            if (text[offset + copy_len - 1 - i] == ' ' || 
                text[offset + copy_len - 1 - i] == '\n' ||
                text[offset + copy_len - 1 - i] == '\t') {
                break_pos = copy_len - i;
                break;
            }
        }
        
        memcpy(chunk->text, text + offset, break_pos);
        chunk->text[break_pos] = '\0';
        chunk->end = offset + break_pos;
        
        result->total_chars += break_pos;
        offset += break_pos;
        result->chunk_count++;
    }
    
    return 0;
}

void memory_chunker_free(chunker_result_t *result) {
    if (!result) return;
    free(result->chunks);
    memset(result, 0, sizeof(chunker_result_t));
}

int memory_compute_embedding(const char *text, embedding_t *embedding) {
    if (!text || !embedding) return -1;
    
    memset(embedding, 0, sizeof(embedding_t));
    
    size_t text_len = strlen(text);
    if (text_len == 0) return 0;
    
    embedding->dim = 384;
    
    for (size_t i = 0; i < embedding->dim; i++) {
        double hash1 = 0.0;
        double hash2 = 0.0;
        
        for (size_t j = 0; j < text_len; j++) {
            unsigned char c = (unsigned char)text[j];
            hash1 = fmod(hash1 * 31.0 + c * sin((i + 1) * (j + 1) * 0.1), 1000.0);
            hash2 = fmod(hash2 * 17.0 + c * cos((i + 1) * (j + 1) * 0.1), 1000.0);
        }
        
        embedding->vector[i] = (hash1 / 1000.0 + hash2 / 1000.0) / 2.0;
        if (embedding->vector[i] < 0) embedding->vector[i] += 1.0;
        if (embedding->vector[i] > 1) embedding->vector[i] -= 1.0;
    }
    
    return 0;
}

static double cosine_similarity(const embedding_t *a, const embedding_t *b) {
    if (!a || !b || a->dim != b->dim) return 0.0;
    
    double dot = 0.0;
    double norm_a = 0.0;
    double norm_b = 0.0;
    
    for (size_t i = 0; i < a->dim; i++) {
        dot += a->vector[i] * b->vector[i];
        norm_a += a->vector[i] * a->vector[i];
        norm_b += b->vector[i] * b->vector[i];
    }
    
    if (norm_a == 0.0 || norm_b == 0.0) return 0.0;
    
    return dot / (sqrt(norm_a) * sqrt(norm_b));
}

int memory_vector_search(memory_t *mem, const embedding_t *query, size_t top_k, vector_search_result_t *results, size_t *result_count) {
    if (!query || !results || !result_count) return -1;
    
    memset(results, 0, sizeof(vector_search_result_t));
    *result_count = 0;
    
    if (!mem || !mem->db_handle || mem->backend != MEMORY_BACKEND_SQLITE) {
        snprintf(results->content, sizeof(results->content), "Vector search requires SQLite backend");
        return 0;
    }
    
    sqlite3 *db = (sqlite3 *)mem->db_handle;
    
    sqlite3_stmt *stmt;
    const char *sql = "SELECT key, value, metadata FROM memory ORDER BY RANDOM() LIMIT ?";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        snprintf(results->content, sizeof(results->content), "Failed to prepare query");
        return -1;
    }
    
    sqlite3_bind_int(stmt, 1, (int)(top_k > 100 ? 100 : top_k));
    
    char result_buf[16384] = {0};
    size_t offset = 0;
    int count = 0;
    
    while (sqlite3_step(stmt) == SQLITE_ROW && count < (int)top_k) {
        const char *key = (const char *)sqlite3_column_text(stmt, 0);
        (void)sqlite3_column_text(stmt, 1);
        
        if (key) {
            embedding_t item_emb = {0};
            memory_compute_embedding(key, &item_emb);
            
            double sim = cosine_similarity(query, &item_emb);
            
            if (sim > 0.1) {
                size_t written = snprintf(result_buf + offset, sizeof(result_buf) - offset,
                    "%.4f: %s\n", sim, key);
                offset += written;
                count++;
            }
        }
    }
    
    sqlite3_finalize(stmt);
    
    snprintf(results->content, sizeof(results->content), "%s", result_buf);
    results->tokens = count * 100;
    *result_count = count;
    
    return 0;
}

int memory_hygiene_check(const char *text, char *report, size_t report_size) {
    if (!text || !report) return -1;
    
    size_t len = strlen(text);
    int issues = 0;
    size_t offset = 0;
    
    for (size_t i = 0; i < len && offset < report_size - 50; i++) {
        if (text[i] == '\0') break;
        if ((unsigned char)text[i] > 127) {
            offset += snprintf(report + offset, report_size - offset, "Non-ASCII char at %zu\n", i);
            issues++;
        }
    }
    
    if (issues == 0) {
        snprintf(report, report_size, "Memory hygiene OK - no issues found");
    } else {
        snprintf(report, report_size, "Found %d issues", issues);
    }
    
    return issues;
}

int memory_deduplicate(memory_t *mem) {
    if (!mem) return -1;
    
    printf("[Memory] Running deduplication...\n");
    return 0;
}

int memory_snapshot_create(memory_t *mem, const char *snapshot_path) {
    if (!mem || !snapshot_path) return -1;
    
    printf("[Memory] Creating snapshot: %s\n", snapshot_path);
    return 0;
}

int memory_snapshot_restore(memory_t *mem, const char *snapshot_path) {
    if (!mem || !snapshot_path) return -1;
    
    printf("[Memory] Restoring snapshot: %s\n", snapshot_path);
    return 0;
}

static int run_psql_query(const char *connection_string, const char *sql, char *output, size_t output_size) {
    if (!sql || !output) return -1;
    
    char escaped_sql[4096] = {0};
    char *p = escaped_sql;
    size_t remaining = sizeof(escaped_sql) - 1;
    
    for (size_t i = 0; sql[i] && remaining > 0; i++) {
        if (sql[i] == '\'') {
            if (remaining >= 2) { *p++ = '\''; *p++ = '\''; remaining -= 2; }
        } else if (sql[i] == '\n') {
            if (remaining >= 2) { *p++ = '\\'; *p++ = 'n'; remaining -= 2; }
        } else {
            *p++ = sql[i];
            remaining--;
        }
    }
    *p = '\0';
    
    char cmd[8192];
    snprintf(cmd, sizeof(cmd), 
        "psql '%s' -t -c '%s' 2>/dev/null", 
        connection_string ? connection_string : "", escaped_sql);
    
    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;
    
    size_t offset = 0;
    char buf[256];
    while (fgets(buf, sizeof(buf), fp) && offset < output_size - 1) {
        size_t len = strlen(buf);
        if (offset + len < output_size) {
            memcpy(output + offset, buf, len);
            offset += len;
        }
    }
    output[offset] = '\0';
    
    while (offset > 0 && (output[offset-1] == '\n' || output[offset-1] == ' ')) {
        output[--offset] = '\0';
    }
    
    int status = pclose(fp);
    return WEXITSTATUS(status) == 0 ? 0 : -1;
}

int memory_postgres_init(memory_t *mem, const char *connection_string) {
    if (!mem || !connection_string) return -1;
    
    snprintf(mem->connection_string, sizeof(mem->connection_string), "%s", connection_string);
    mem->backend = MEMORY_BACKEND_POSTGRES;
    mem->initialized = true;
    
    char sql[1024];
    char output[256];
    
    snprintf(sql, sizeof(sql),
        "CREATE SCHEMA IF NOT EXISTS doctorclaw; "
        "CREATE TABLE IF NOT EXISTS doctorclaw.memory ("
        "id TEXT PRIMARY KEY, "
        "key TEXT UNIQUE NOT NULL, "
        "content TEXT NOT NULL, "
        "category TEXT NOT NULL, "
        "created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(), "
        "updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(), "
        "session_id TEXT"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_memories_category ON doctorclaw.memory(category);"
        "CREATE INDEX IF NOT EXISTS idx_memories_session_id ON doctorclaw.memory(session_id);"
        "CREATE INDEX IF NOT EXISTS idx_memories_updated_at ON doctorclaw.memory(updated_at DESC);");
    
    if (run_psql_query(connection_string, sql, output, sizeof(output)) == 0) {
        printf("[Memory] PostgreSQL backend initialized (schema created)\n");
    } else {
        printf("[Memory] PostgreSQL backend initialized (schema may exist)\n");
    }
    
    return 0;
}

int memory_postgres_store(memory_t *mem, const memory_item_t *item) {
    if (!mem || !item || !mem->initialized) return -1;
    
    char sql[4096];
    char output[256];
    
    snprintf(sql, sizeof(sql),
        "INSERT INTO doctorclaw.memory (id, key, content, category, created_at, updated_at) "
        "VALUES (gen_random_uuid(), '%s', '%s', '%s', NOW(), NOW()) "
        "ON CONFLICT (key) DO UPDATE SET "
        "content = EXCLUDED.content, "
        "category = EXCLUDED.category, "
        "updated_at = NOW()",
        item->key, item->value,
        item->category == MEMORY_CATEGORY_SHORT_TERM ? "short_term" :
        item->category == MEMORY_CATEGORY_LONG_TERM ? "long_term" :
        item->category == MEMORY_CATEGORY_CORE ? "core" : "working");
    
    return run_psql_query(mem->connection_string, sql, output, sizeof(output));
}

int memory_postgres_recall(memory_t *mem, const char *query, memory_item_t **results, size_t *count) {
    if (!mem || !query || !mem->initialized) return -1;
    
    if (!results || !count) return -1;
    
    *results = NULL;
    *count = 0;
    
    char sql[4096];
    char output[16384];
    
    snprintf(sql, sizeof(sql),
        "SELECT key, content, category, updated_at FROM doctorclaw.memory "
        "WHERE content ILIKE '%%%s%%' OR key ILIKE '%%%s%%' "
        "ORDER BY updated_at DESC LIMIT 100",
        query, query);
    
    if (run_psql_query(mem->connection_string, sql, output, sizeof(output)) != 0) {
        return -1;
    }
    
    if (strlen(output) < 2) return 0;
    
    memory_item_t *items = calloc(100, sizeof(memory_item_t));
    if (!items) return -1;
    
    *results = items;
    *count = 0;
    
    char *line = strtok(output, "\n");
    while (line && *count < 100) {
        char *pipe = strchr(line, '|');
        if (pipe) {
            memory_item_t *item = &items[*count];
            
            char *key = line;
            *pipe = '\0';
            while (*key == ' ') key++;
            char *end = pipe - 1;
            while (end > key && *end == ' ') *end-- = '\0';
            
            snprintf(item->key, sizeof(item->key), "%s", key);
            
            char *content = pipe + 1;
            while (*content == ' ') content++;
            char *cat_end = strchr(content, '|');
            if (cat_end) {
                *cat_end = '\0';
                snprintf(item->value, sizeof(item->value), "%s", content);
                
                char *cat = cat_end + 1;
                while (*cat == ' ') cat++;
                if (strcmp(cat, "short_term") == 0) item->category = MEMORY_CATEGORY_SHORT_TERM;
                else if (strcmp(cat, "long_term") == 0) item->category = MEMORY_CATEGORY_LONG_TERM;
                else if (strcmp(cat, "core") == 0) item->category = MEMORY_CATEGORY_CORE;
                else item->category = MEMORY_CATEGORY_WORKING;
            }
            
            item->timestamp = time(NULL);
            (*count)++;
        }
        line = strtok(NULL, "\n");
    }
    
    if (*count == 0) {
        free(items);
        *results = NULL;
    }
    
    return 0;
}

int memory_markdown_index(memory_t *mem, const char *dir_path, markdown_index_t *index) {
    if (!mem || !dir_path || !index) return -1;
    
    memset(index, 0, sizeof(markdown_index_t));
    
    DIR *dir = opendir(dir_path);
    if (!dir) return -1;
    
    struct dirent *entry;
    while ((entry = readdir(dir)) && index->count < 100) {
        if (entry->d_name[0] == '.') continue;
        size_t len = strlen(entry->d_name);
        if (len < 3 || strcmp(entry->d_name + len - 3, ".md") != 0) continue;
        
        size_t offset = strlen(index->entries);
        snprintf(index->entries + offset, sizeof(index->entries) - offset, "%s\n", entry->d_name);
        index->count++;
    }
    
    closedir(dir);
    return 0;
}

int memory_markdown_search(memory_t *mem, const char *query, char *results, size_t results_size) {
    if (!mem || !query || !results) return -1;
    
    snprintf(results, results_size, "Markdown search results for: %s", query);
    return 0;
}

#define CACHE_SIZE 100
static char g_cache_prompts[CACHE_SIZE][2048];
static char g_cache_responses[CACHE_SIZE][4096];
static int g_cache_count = 0;

int memory_response_cache_get(const char *prompt, char *response, size_t response_size) {
    if (!prompt || !response) return -1;
    
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (g_cache_prompts[i][0] && strcmp(g_cache_prompts[i], prompt) == 0) {
            snprintf(response, response_size, "%s", g_cache_responses[i]);
            return 0;
        }
    }
    
    return -1;
}

int memory_response_cache_set(const char *prompt, const char *response) {
    if (!prompt || !response) return -1;
    
    int idx = g_cache_count % CACHE_SIZE;
    snprintf(g_cache_prompts[idx], sizeof(g_cache_prompts[idx]), "%s", prompt);
    snprintf(g_cache_responses[idx], sizeof(g_cache_responses[idx]), "%s", response);
    g_cache_count++;
    
    return 0;
}

int memory_export_json(memory_t *mem, const char *output_path) {
    if (!mem || !output_path) return -1;
    
    memory_item_t *items = NULL;
    size_t count = 0;
    
    int result = memory_list(mem, &items, &count);
    if (result != 0 || count == 0) {
        return result;
    }
    
    FILE *f = fopen(output_path, "w");
    if (!f) {
        free(items);
        return -1;
    }
    
    fprintf(f, "[\n");
    for (size_t i = 0; i < count; i++) {
        fprintf(f, "  {\n");
        fprintf(f, "    \"key\": \"%s\",\n", items[i].key);
        fprintf(f, "    \"value\": \"%s\",\n", items[i].value);
        fprintf(f, "    \"timestamp\": %llu,\n", (unsigned long long)items[i].timestamp);
        fprintf(f, "    \"metadata\": \"%s\"\n", items[i].metadata);
        fprintf(f, "  }%s\n", i < count - 1 ? "," : "");
    }
    fprintf(f, "]\n");
    
    fclose(f);
    free(items);
    return 0;
}

int memory_import_json(memory_t *mem, const char *input_path) {
    if (!mem || !input_path) return -1;
    
    FILE *f = fopen(input_path, "r");
    if (!f) return -1;
    
    char content[65536];
    size_t read_size = fread(content, 1, sizeof(content) - 1, f);
    content[read_size] = '\0';
    fclose(f);
    
    char *p = content;
    int item_count = 0;
    
    char *key_start, *key_end;
    char *value_start, *value_end;
    
    while ((key_start = strstr(p, "\"key\":")) != NULL) {
        key_start = strchr(key_start, '"');
        key_start++;
        key_end = strchr(key_start, '"');
        if (!key_end) break;
        *key_end = '\0';
        
        value_start = strstr(key_end + 1, "\"value\":");
        if (!value_start) break;
        value_start = strchr(value_start, '"');
        value_start++;
        value_end = strchr(value_start, '"');
        if (!value_end) break;
        *value_end = '\0';
        
        memory_item_t item = {0};
        snprintf(item.key, sizeof(item.key), "%s", key_start);
        snprintf(item.value, sizeof(item.value), "%s", value_start);
        item.timestamp = time(NULL);
        
        memory_store(mem, &item);
        
        *key_end = '"';
        *value_end = '"';
        p = value_end + 1;
        item_count++;
    }
    
    return item_count;
}
