/**
 * Muninn-style cognitive database — C23 implementation.
 * Inspired by https://github.com/scrypster/muninndb
 * Engrams, temporal priority (Ebbinghaus-style), Hebbian associations, SQLite-backed.
 */

#include "muninn.h"
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#define ENGRAMS_TABLE "muninn_engrams"
#define FTS_TABLE "muninn_fts"
#define ASSOC_TABLE "muninn_associations"
#define HEBBIAN_DELTA 0.02f
#define HEBBIAN_MAX 1.0f
#define DECAY_HALFLIFE_DAYS 7.0
#define MAX_ACTIVATE_CANDIDATES 256

static int schema_init(sqlite3 *db) {
    const char *sql =
        "CREATE TABLE IF NOT EXISTS " ENGRAMS_TABLE " ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  vault TEXT NOT NULL,"
        "  concept TEXT NOT NULL,"
        "  content TEXT NOT NULL,"
        "  confidence REAL NOT NULL DEFAULT 0.8,"
        "  access_count INTEGER NOT NULL DEFAULT 0,"
        "  last_access_sec INTEGER NOT NULL DEFAULT 0,"
        "  created_sec INTEGER NOT NULL,"
        "  state INTEGER NOT NULL DEFAULT 0,"
        "  type INTEGER NOT NULL DEFAULT 0,"
        "  tags TEXT"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_muninn_vault ON " ENGRAMS_TABLE "(vault);"
        "CREATE INDEX IF NOT EXISTS idx_muninn_last_access ON " ENGRAMS_TABLE "(last_access_sec);"
        "CREATE VIRTUAL TABLE IF NOT EXISTS " FTS_TABLE " USING fts5(concept, content, content='" ENGRAMS_TABLE "', content_rowid='id');";
    char *err = NULL;
    if (sqlite3_exec(db, sql, NULL, NULL, &err) != SQLITE_OK) {
        if (err) { (void)fprintf(stderr, "[Muninn] schema: %s\n", err); sqlite3_free(err); }
        return -1;
    }
    const char *trigger_sql =
        "CREATE TRIGGER IF NOT EXISTS muninn_ai AFTER INSERT ON " ENGRAMS_TABLE " BEGIN "
        "  INSERT INTO " FTS_TABLE "(rowid, concept, content) VALUES (new.id, new.concept, new.content); "
        "END;"
        "CREATE TRIGGER IF NOT EXISTS muninn_ad AFTER DELETE ON " ENGRAMS_TABLE " BEGIN "
        "  INSERT INTO " FTS_TABLE "(" FTS_TABLE ", rowid, concept, content) VALUES('delete', old.id, old.concept, old.content); "
        "END;"
        "CREATE TRIGGER IF NOT EXISTS muninn_au AFTER UPDATE ON " ENGRAMS_TABLE " BEGIN "
        "  INSERT INTO " FTS_TABLE "(" FTS_TABLE ", rowid, concept, content) VALUES('delete', old.id, old.concept, old.content); "
        "  INSERT INTO " FTS_TABLE "(rowid, concept, content) VALUES (new.id, new.concept, new.content); "
        "END;";
    if (sqlite3_exec(db, trigger_sql, NULL, NULL, &err) != SQLITE_OK) {
        if (err) { sqlite3_free(err); }
        return -1;
    }
    const char *assoc_sql =
        "CREATE TABLE IF NOT EXISTS " ASSOC_TABLE " ("
        "  from_id INTEGER NOT NULL,"
        "  to_id INTEGER NOT NULL,"
        "  weight REAL NOT NULL DEFAULT 0.1,"
        "  PRIMARY KEY (from_id, to_id),"
        "  CHECK (from_id != to_id)"
        ");";
    if (sqlite3_exec(db, assoc_sql, NULL, NULL, &err) != SQLITE_OK) {
        if (err) { sqlite3_free(err); return -1; }
    }
    return 0;
}

static void engram_from_row(sqlite3_stmt *stmt, muninn_engram_t *e, int64_t rowid) {
    memset(e, 0, sizeof(*e));
    (void)snprintf(e->id, sizeof(e->id), "%lld", (long long)rowid);
    const char *v = (const char *)sqlite3_column_text(stmt, 1);
    if (v) (void)snprintf(e->vault, sizeof(e->vault), "%s", v);
    v = (const char *)sqlite3_column_text(stmt, 2);
    if (v) (void)snprintf(e->concept, sizeof(e->concept), "%s", v);
    v = (const char *)sqlite3_column_text(stmt, 3);
    if (v) (void)snprintf(e->content, sizeof(e->content), "%s", v);
    e->confidence = (float)sqlite3_column_double(stmt, 4);
    e->access_count = (uint32_t)sqlite3_column_int(stmt, 5);
    e->last_access_sec = (uint64_t)sqlite3_column_int64(stmt, 6);
    e->created_sec = (uint64_t)sqlite3_column_int64(stmt, 7);
    e->state = (muninn_state_t)sqlite3_column_int(stmt, 8);
    e->type = (muninn_type_t)sqlite3_column_int(stmt, 9);
    const char *tags_str = (const char *)sqlite3_column_text(stmt, 10);
    e->tag_count = 0;
    if (tags_str) {
        char buf[512];
        (void)snprintf(buf, sizeof(buf), "%s", tags_str);
        for (char *tok = strtok(buf, ","); tok && e->tag_count < MUNINN_TAG_MAX; tok = strtok(NULL, ",")) {
            size_t len = strlen(tok);
            while (len > 0 && (tok[len-1] == ' ' || tok[len-1] == '\t')) tok[--len] = '\0';
            (void)snprintf(e->tags[e->tag_count], sizeof(e->tags[0]), "%s", tok);
            e->tag_count++;
        }
    }
}

int muninn_init(muninn_t *m, const char *path) {
    if (!m || !path) return -1;
    memset(m, 0, sizeof(*m));
    (void)snprintf(m->path, sizeof(m->path), "%s", path);
    sqlite3 *db = NULL;
    if (sqlite3_open(path, &db) != SQLITE_OK) {
        (void)fprintf(stderr, "[Muninn] open failed: %s\n", sqlite3_errmsg(db));
        if (db) sqlite3_close(db);
        return -1;
    }
    if (schema_init(db) != 0) {
        sqlite3_close(db);
        return -1;
    }
    m->db = db;
    m->initialized = true;
    return 0;
}

void muninn_free(muninn_t *m) {
    if (!m) return;
    if (m->db) {
        sqlite3_close((sqlite3 *)m->db);
        m->db = NULL;
    }
    m->initialized = false;
}

int muninn_write(muninn_t *m, const char *vault, const char *concept, const char *content,
                 const char *const *tags, size_t n_tags, char *out_id, size_t id_size) {
    if (!m || !m->db || !vault || !concept || !content || !out_id || id_size == 0) return -1;
    sqlite3 *db = (sqlite3 *)m->db;
    uint64_t now = (uint64_t)time(NULL);
    const char *sql = "INSERT INTO " ENGRAMS_TABLE " (vault, concept, content, confidence, access_count, last_access_sec, created_sec, state, type, tags) VALUES (?1,?2,?3,0.8,0,?4,?5,0,0,?6)";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, vault, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, concept, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, content, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, (sqlite3_int64)now);
    sqlite3_bind_int64(stmt, 5, (sqlite3_int64)now);
    char tags_buf[MUNINN_TAG_MAX * (MUNINN_TAG_LEN + 2)];
    tags_buf[0] = '\0';
    if (tags && n_tags > 0) {
        for (size_t i = 0; i < n_tags && i < MUNINN_TAG_MAX; i++) {
            if (i) strcat(tags_buf, ",");
            strncat(tags_buf, tags[i], sizeof(tags_buf) - strlen(tags_buf) - 1);
        }
    }
    sqlite3_bind_text(stmt, 6, tags_buf, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return -1;
    int64_t rowid = sqlite3_last_insert_rowid(db);
    (void)snprintf(out_id, id_size, "%lld", (long long)rowid);
    return 0;
}

int muninn_read(muninn_t *m, const char *vault, const char *id, muninn_engram_t *out) {
    if (!m || !m->db || !vault || !id || !out) return -1;
    sqlite3 *db = (sqlite3 *)m->db;
    const char *sql = "SELECT id, vault, concept, content, confidence, access_count, last_access_sec, created_sec, state, type, tags FROM " ENGRAMS_TABLE " WHERE id = ?1 AND vault = ?2 AND (state = 0 OR state = 4)";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, vault, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return -1;
    }
    int64_t rowid = sqlite3_column_int64(stmt, 0);
    engram_from_row(stmt, out, rowid);
    sqlite3_finalize(stmt);
    return 0;
}

/* Temporal priority: higher access_count and more recent last_access => higher score. */
static double temporal_priority(uint32_t access_count, uint64_t last_access_sec, uint64_t now_sec) {
    if (last_access_sec == 0) last_access_sec = now_sec;
    double days_ago = (double)(now_sec - last_access_sec) / 86400.0;
    double decay = exp(-0.693 * days_ago / DECAY_HALFLIFE_DAYS); /* half-life decay */
    double freq = (double)(1 + access_count);
    return freq * decay;
}

int muninn_activate(muninn_t *m, const char *vault, const char *context, size_t top_n,
                    muninn_engram_t *results, size_t *result_count) {
    if (!m || !m->db || !vault || !context || !results || !result_count) return -1;
    *result_count = 0;
    if (top_n == 0 || top_n > MUNINN_ACTIVATE_MAX) top_n = MUNINN_ACTIVATE_MAX;
    sqlite3 *db = (sqlite3 *)m->db;
    uint64_t now = (uint64_t)time(NULL);

    /* FTS: get candidate rowids matching context. */
    char query[1024];
    (void)snprintf(query, sizeof(query), "SELECT rowid FROM " FTS_TABLE " WHERE " FTS_TABLE " MATCH ? LIMIT %d", MAX_ACTIVATE_CANDIDATES);
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, query, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, context, -1, SQLITE_TRANSIENT);
    int64_t candidate_ids[MAX_ACTIVATE_CANDIDATES];
    size_t nc = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && nc < MAX_ACTIVATE_CANDIDATES) {
        candidate_ids[nc++] = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);

    /* If FTS returns nothing, fall back to all engrams in vault (by recency). */
    if (nc == 0) {
        char fallback_buf[256];
        (void)snprintf(fallback_buf, sizeof(fallback_buf), "SELECT id FROM " ENGRAMS_TABLE " WHERE vault = ?1 AND (state = 0 OR state = 4) ORDER BY last_access_sec DESC, access_count DESC LIMIT %d", MAX_ACTIVATE_CANDIDATES);
        if (sqlite3_prepare_v2(db, fallback_buf, -1, &stmt, NULL) != SQLITE_OK) return 0;
        sqlite3_bind_text(stmt, 1, vault, -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW && nc < MAX_ACTIVATE_CANDIDATES) {
            candidate_ids[nc++] = sqlite3_column_int64(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    /* Load each candidate and compute score = temporal_priority * confidence. */
    typedef struct { int64_t id; double score; } scored_t;
    scored_t scored[MAX_ACTIVATE_CANDIDATES];
    size_t ns = 0;
    const char *row_sql = "SELECT id, vault, concept, content, confidence, access_count, last_access_sec, created_sec, state, type, tags FROM " ENGRAMS_TABLE " WHERE id = ?1 AND vault = ?2";
    if (sqlite3_prepare_v2(db, row_sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    for (size_t i = 0; i < nc; i++) {
        sqlite3_reset(stmt);
        sqlite3_bind_int64(stmt, 1, candidate_ids[i]);
        sqlite3_bind_text(stmt, 2, vault, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) != SQLITE_ROW) continue;
        uint32_t ac = (uint32_t)sqlite3_column_int(stmt, 5);
        uint64_t las = (uint64_t)sqlite3_column_int64(stmt, 6);
        float conf = (float)sqlite3_column_double(stmt, 4);
        double tp = temporal_priority(ac, las, now);
        scored[ns].id = candidate_ids[i];
        scored[ns].score = tp * (double)conf;
        ns++;
    }
    sqlite3_finalize(stmt);

    /* Sort by score descending (simple insertion sort for small ns). */
    for (size_t i = 1; i < ns; i++) {
        scored_t t = scored[i];
        size_t j = i;
        while (j > 0 && scored[j-1].score < t.score) {
            scored[j] = scored[j-1];
            j--;
        }
        scored[j] = t;
    }

    /* Copy top_n into results and record access + Hebbian. */
    size_t n = (top_n < ns) ? top_n : ns;
    int64_t returned_ids[MUNINN_ACTIVATE_MAX];
    size_t nret = 0;
    const char *load_sql = "SELECT id, vault, concept, content, confidence, access_count, last_access_sec, created_sec, state, type, tags FROM " ENGRAMS_TABLE " WHERE id = ?1";
    sqlite3_stmt *load_stmt = NULL;
    if (sqlite3_prepare_v2(db, load_sql, -1, &load_stmt, NULL) != SQLITE_OK) return -1;
    for (size_t i = 0; i < n; i++) {
        sqlite3_reset(load_stmt);
        sqlite3_bind_int64(load_stmt, 1, scored[i].id);
        if (sqlite3_step(load_stmt) != SQLITE_ROW) continue;
        engram_from_row(load_stmt, &results[*result_count], scored[i].id);
        returned_ids[nret++] = scored[i].id;
        (*result_count)++;
    }
    sqlite3_finalize(load_stmt);

    /* Record access (bump access_count, last_access_sec) for each returned. */
    sqlite3_stmt *acc_stmt = NULL;
    const char *acc_sql = "UPDATE " ENGRAMS_TABLE " SET access_count = access_count + 1, last_access_sec = ?1 WHERE id = ?2";
    if (sqlite3_prepare_v2(db, acc_sql, -1, &acc_stmt, NULL) == SQLITE_OK) {
        for (size_t i = 0; i < nret; i++) {
            sqlite3_reset(acc_stmt);
            sqlite3_bind_int64(acc_stmt, 1, (sqlite3_int64)now);
            sqlite3_bind_int64(acc_stmt, 2, returned_ids[i]);
            (void)sqlite3_step(acc_stmt);
        }
        sqlite3_finalize(acc_stmt);
    }

    /* Hebbian: strengthen associations between each pair of returned engrams. */
    if (nret >= 2) {
        sqlite3_stmt *heb_stmt = NULL;
        char heb_buf[320];
        (void)snprintf(heb_buf, sizeof(heb_buf), "INSERT INTO " ASSOC_TABLE " (from_id, to_id, weight) VALUES (?1,?2,%.4f) ON CONFLICT(from_id, to_id) DO UPDATE SET weight = MIN(%.2f, weight + %.4f)", (double)HEBBIAN_DELTA, (double)HEBBIAN_MAX, (double)HEBBIAN_DELTA);
        if (sqlite3_prepare_v2(db, heb_buf, -1, &heb_stmt, NULL) == SQLITE_OK) {
            for (size_t i = 0; i < nret; i++) {
                for (size_t j = 0; j < nret; j++) {
                    if (returned_ids[i] == returned_ids[j]) continue;
                    sqlite3_reset(heb_stmt);
                    sqlite3_bind_int64(heb_stmt, 1, returned_ids[i]);
                    sqlite3_bind_int64(heb_stmt, 2, returned_ids[j]);
                    (void)sqlite3_step(heb_stmt);
                }
            }
            sqlite3_finalize(heb_stmt);
        }
    }

    return 0;
}

int muninn_record_access(muninn_t *m, const char *vault, const char *id) {
    if (!m || !m->db || !vault || !id) return -1;
    uint64_t now = (uint64_t)time(NULL);
    sqlite3 *db = (sqlite3 *)m->db;
    const char *sql = "UPDATE " ENGRAMS_TABLE " SET access_count = access_count + 1, last_access_sec = ?1 WHERE id = ?2 AND vault = ?3";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int64(stmt, 1, (sqlite3_int64)now);
    sqlite3_bind_text(stmt, 2, id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, vault, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int muninn_reinforce(muninn_t *m, const char *id, float delta) {
    if (!m || !m->db || !id) return -1;
    sqlite3 *db = (sqlite3 *)m->db;
    const char *sql = "UPDATE " ENGRAMS_TABLE " SET confidence = MIN(1.0, confidence + ?1) WHERE id = ?2";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_double(stmt, 1, (double)delta);
    sqlite3_bind_text(stmt, 2, id, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int muninn_contradict(muninn_t *m, const char *id) {
    if (!m || !m->db || !id) return -1;
    sqlite3 *db = (sqlite3 *)m->db;
    const char *sql = "UPDATE " ENGRAMS_TABLE " SET confidence = MAX(0.0, confidence - 0.1) WHERE id = ?1";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, id, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int muninn_soft_delete(muninn_t *m, const char *vault, const char *id) {
    if (!m || !m->db || !vault || !id) return -1;
    sqlite3 *db = (sqlite3 *)m->db;
    const char *sql = "UPDATE " ENGRAMS_TABLE " SET state = 2 WHERE id = ?1 AND vault = ?2"; /* 2 = SOFT_DELETED */
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, vault, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int muninn_list_vaults(muninn_t *m, char vaults[][MUNINN_VAULT_MAX], size_t max_count, size_t *count) {
    if (!m || !m->db || !vaults || !count) return -1;
    *count = 0;
    sqlite3 *db = (sqlite3 *)m->db;
    const char *sql = "SELECT DISTINCT vault FROM " ENGRAMS_TABLE " ORDER BY vault";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    while (sqlite3_step(stmt) == SQLITE_ROW && *count < max_count) {
        const char *v = (const char *)sqlite3_column_text(stmt, 0);
        if (v) (void)snprintf(vaults[*count], MUNINN_VAULT_MAX, "%s", v);
        (*count)++;
    }
    sqlite3_finalize(stmt);
    return 0;
}
