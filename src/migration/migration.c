#include "migration.h"
#include "memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>

static const char *source_names[] = {
    "claude",
    "openai",
    "ollama",
    "llamacpp",
    "generic",
    NULL
};

static const char *state_names[] = {
    "pending",
    "scanning",
    "migrating",
    "completed",
    "failed"
};

int migration_manager_init(migration_manager_t *mgr) {
    if (!mgr) return -1;
    memset(mgr, 0, sizeof(migration_manager_t));
    return 0;
}

int migration_add(migration_manager_t *mgr, migration_source_t source, const char *source_path, const char *dest_path) {
    if (!mgr || !source_path || !dest_path) return -1;
    if (mgr->migration_count >= 16) return -1;
    
    migration_t *mig = &mgr->migrations[mgr->migration_count];
    mig->source = source;
    mig->state = MIGRATION_STATE_PENDING;
    snprintf(mig->source_path, sizeof(mig->source_path), "%s", source_path);
    snprintf(mig->dest_path, sizeof(mig->dest_path), "%s", dest_path);
    mig->items_scanned = 0;
    mig->items_migrated = 0;
    mig->items_failed = 0;
    mig->error[0] = '\0';
    
    mgr->migration_count++;
    return 0;
}

static int migration_scan_directory(migration_t *mig) {
    DIR *dir = opendir(mig->source_path);
    if (!dir) {
        snprintf(mig->error, sizeof(mig->error), "Cannot open source directory");
        mig->state = MIGRATION_STATE_FAILED;
        return -1;
    }
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        mig->items_scanned++;
    }
    
    closedir(dir);
    return 0;
}

/* Minimal JSON key-value parse: advance *p past whitespace and optional comma; return 1 if found key/val. */
static int parse_one_kv(const char **p, char *key, size_t key_len, char *value, size_t value_len) {
    const char *s = *p;
    while (*s && (*s == ' ' || *s == '\t' || *s == '\n' || *s == ',' || *s == '{' || *s == '}')) s++;
    if (*s != '"') return 0;
    s++;
    size_t i = 0;
    while (*s && *s != '"' && i + 1 < key_len) {
        if (*s == '\\') s++;
        if (*s) key[i++] = *s++;
    }
    key[i] = '\0';
    if (*s != '"') return 0;
    s++;
    while (*s && (*s == ' ' || *s == '\t' || *s == ':')) s++;
    if (*s != '"') return 0;
    s++;
    i = 0;
    while (*s && *s != '"' && i + 1 < value_len) {
        if (*s == '\\' && (s[1] == '"' || s[1] == '\\')) s++;
        if (*s) value[i++] = *s++;
    }
    value[i] = '\0';
    *p = *s == '"' ? s + 1 : s;
    return 1;
}

static int migration_import_generic_json(migration_t *mig) {
    FILE *f = fopen(mig->source_path, "r");
    if (!f) {
        snprintf(mig->error, sizeof(mig->error), "Cannot open JSON file");
        mig->state = MIGRATION_STATE_FAILED;
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > 1024 * 1024) {
        fclose(f);
        snprintf(mig->error, sizeof(mig->error), "Invalid file size");
        mig->state = MIGRATION_STATE_FAILED;
        return -1;
    }
    char *buf = malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return -1;
    }
    size_t n = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[n] = '\0';

    char db_path[MAX_MIGRATION_PATH];
    snprintf(db_path, sizeof(db_path), "%s/memory.db", mig->dest_path);

    memory_t mem = {0};
    if (memory_init(&mem, MEMORY_BACKEND_SQLITE, db_path) != 0) {
        snprintf(mig->error, sizeof(mig->error), "Memory init failed");
        mig->state = MIGRATION_STATE_FAILED;
        free(buf);
        return -1;
    }

    const char *p = buf;
    char key[MAX_MIGRATION_NAME * 4];
    char value[16384];
    while (parse_one_kv(&p, key, sizeof(key), value, sizeof(value))) {
        mig->items_scanned++;
        memory_item_t item = {0};
        snprintf(item.key, sizeof(item.key), "%s", key);
        snprintf(item.value, sizeof(item.value), "%s", value);
        item.timestamp = (uint64_t)time(NULL);
        item.category = MEMORY_CATEGORY_LONG_TERM;
        if (memory_store(&mem, &item) == 0) {
            mig->items_migrated++;
        } else {
            mig->items_failed++;
        }
    }
    memory_free(&mem);
    free(buf);
    return 0;
}

static int migration_copy_files(migration_t *mig) {
    DIR *dir = opendir(mig->source_path);
    if (!dir) return -1;
    
    struct dirent *entry;
    char src_file[1024];
    char dst_file[1024];
    
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        
        snprintf(src_file, sizeof(src_file), "%s/%s", mig->source_path, entry->d_name);
        snprintf(dst_file, sizeof(dst_file), "%s/%s", mig->dest_path, entry->d_name);
        
        struct stat st;
        if (stat(src_file, &st) == 0 && S_ISREG(st.st_mode)) {
            FILE *in = fopen(src_file, "rb");
            FILE *out = fopen(dst_file, "wb");
            if (in && out) {
                char buf[4096];
                size_t n;
                while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
                    fwrite(buf, 1, n, out);
                }
                fclose(out);
                mig->items_migrated++;
            }
            if (in) fclose(in);
        }
    }
    
    closedir(dir);
    return 0;
}

int migration_execute(migration_manager_t *mgr, const char *name) {
    if (!mgr || !name) return -1;
    
    migration_t *mig = NULL;
    for (size_t i = 0; i < mgr->migration_count; i++) {
        if (strncmp(mgr->migrations[i].source_path, name, strlen(name)) == 0 ||
            strncmp(mgr->migrations[i].dest_path, name, strlen(name)) == 0) {
            mig = &mgr->migrations[i];
            break;
        }
    }
    
    if (!mig) return -1;

    /* Generic or Ollama/Claude/OpenAI JSON export: same key-value import. */
    if (mig->source == MIGRATION_SOURCE_GENERIC || mig->source == MIGRATION_SOURCE_OLLAMA
        || mig->source == MIGRATION_SOURCE_CLAUDE || mig->source == MIGRATION_SOURCE_OPENAI) {
        size_t len = strlen(mig->source_path);
        if (len >= 5 && strcmp(mig->source_path + len - 5, ".json") == 0) {
            mig->state = MIGRATION_STATE_SCANNING;
            mig->items_scanned = 0;
            mig->items_migrated = 0;
            mig->items_failed = 0;
            mig->state = MIGRATION_STATE_MIGRATING;
            if (migration_import_generic_json(mig) != 0) {
                return -1;
            }
            mig->state = MIGRATION_STATE_COMPLETED;
            return 0;
        }
    }

    mig->state = MIGRATION_STATE_SCANNING;
    if (migration_scan_directory(mig) != 0) {
        return -1;
    }

    mig->state = MIGRATION_STATE_MIGRATING;
    migration_copy_files(mig);

    mig->state = MIGRATION_STATE_COMPLETED;
    return 0;
}

int migration_status(migration_manager_t *mgr, const char *name, migration_t *out_migration) {
    if (!mgr || !name || !out_migration) return -1;
    
    for (size_t i = 0; i < mgr->migration_count; i++) {
        if (strncmp(mgr->migrations[i].source_path, name, strlen(name)) == 0) {
            *out_migration = mgr->migrations[i];
            return 0;
        }
    }
    return -1;
}

int migration_list(migration_manager_t *mgr, migration_t **out_migrations, size_t *out_count) {
    if (!mgr || !out_migrations || !out_count) return -1;
    *out_migrations = mgr->migrations;
    *out_count = mgr->migration_count;
    return 0;
}

int migration_rollback(migration_manager_t *mgr, const char *name) {
    if (!mgr || !name) return -1;
    
    for (size_t i = 0; i < mgr->migration_count; i++) {
        if (strncmp(mgr->migrations[i].source_path, name, strlen(name)) == 0) {
            migration_t *mig = &mgr->migrations[i];
            
            DIR *dir = opendir(mig->dest_path);
            if (!dir) return -1;
            
            struct dirent *entry;
            char dst_file[1024];
            
            while ((entry = readdir(dir)) != NULL) {
                if (entry->d_name[0] == '.') continue;
                snprintf(dst_file, sizeof(dst_file), "%s/%s", mig->dest_path, entry->d_name);
                remove(dst_file);
            }
            
            closedir(dir);
            mig->state = MIGRATION_STATE_PENDING;
            mig->items_migrated = 0;
            return 0;
        }
    }
    return -1;
}

void migration_manager_free(migration_manager_t *mgr) {
    if (mgr) {
        memset(mgr, 0, sizeof(migration_manager_t));
    }
}

const char *migration_source_name(migration_source_t source) {
    if (source >= 0 && source < MIGRATION_SOURCE_NONE) {
        return source_names[source];
    }
    return "unknown";
}

migration_source_t migration_source_from_name(const char *name) {
    if (!name) return MIGRATION_SOURCE_NONE;
    
    for (int i = 0; source_names[i]; i++) {
        if (strcmp(name, source_names[i]) == 0) {
            return (migration_source_t)i;
        }
    }
    return MIGRATION_SOURCE_NONE;
}

const char *migration_state_name(migration_state_t state) {
    if (state >= 0 && state <= MIGRATION_STATE_FAILED) {
        return state_names[state];
    }
    return "unknown";
}
