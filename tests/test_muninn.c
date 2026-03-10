#include "muninn.h"
#include "test_harness.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void make_temp_db(char *buf, size_t size) {
    snprintf(buf, size, "/tmp/doctorclaw_muninn_test_%d.db", (int)getpid());
}

static int test_muninn_init_free(void) {
    TEST_BEGIN();
    muninn_t m = {0};
    char path[256];
    make_temp_db(path, sizeof(path));
    int r = muninn_init(&m, path);
    ASSERT_EQ(r, 0);
    ASSERT_TRUE(m.initialized);
    ASSERT_NOT_NULL(m.db);
    muninn_free(&m);
    ASSERT_FALSE(m.initialized);
    unlink(path);
    TEST_END();
}

static int test_muninn_write_read(void) {
    TEST_BEGIN();
    muninn_t m = {0};
    char path[256];
    make_temp_db(path, sizeof(path));
    ASSERT_EQ(muninn_init(&m, path), 0);
    char id[MUNINN_ID_MAX];
    int r = muninn_write(&m, "default", "database", "PostgreSQL 15 with pgvector", NULL, 0, id, sizeof(id));
    ASSERT_EQ(r, 0);
    ASSERT_TRUE(strlen(id) > 0);
    muninn_engram_t e = {0};
    r = muninn_read(&m, "default", id, &e);
    ASSERT_EQ(r, 0);
    ASSERT_STR_EQ(e.concept, "database");
    ASSERT_STR_EQ(e.content, "PostgreSQL 15 with pgvector");
    ASSERT_STR_EQ(e.vault, "default");
    muninn_free(&m);
    unlink(path);
    TEST_END();
}

static int test_muninn_activate(void) {
    TEST_BEGIN();
    muninn_t m = {0};
    char path[256];
    make_temp_db(path, sizeof(path));
    ASSERT_EQ(muninn_init(&m, path), 0);
    char id1[MUNINN_ID_MAX], id2[MUNINN_ID_MAX];
    muninn_write(&m, "default", "database", "We use PostgreSQL", NULL, 0, id1, sizeof(id1));
    muninn_write(&m, "default", "api", "REST on port 8475", NULL, 0, id2, sizeof(id2));
    muninn_engram_t results[4];
    size_t n = 0;
    int r = muninn_activate(&m, "default", "what database", 4, results, &n);
    ASSERT_EQ(r, 0);
    ASSERT_GE(n, 1);
    ASSERT_TRUE(strstr(results[0].content, "PostgreSQL") != NULL || strstr(results[0].concept, "database") != NULL);
    muninn_free(&m);
    unlink(path);
    TEST_END();
}

static int test_muninn_record_access(void) {
    TEST_BEGIN();
    muninn_t m = {0};
    char path[256];
    make_temp_db(path, sizeof(path));
    ASSERT_EQ(muninn_init(&m, path), 0);
    char id[MUNINN_ID_MAX];
    muninn_write(&m, "default", "concept", "content", NULL, 0, id, sizeof(id));
    ASSERT_EQ(muninn_record_access(&m, "default", id), 0);
    muninn_engram_t e = {0};
    ASSERT_EQ(muninn_read(&m, "default", id, &e), 0);
    ASSERT_GE(e.access_count, 1u);
    muninn_free(&m);
    unlink(path);
    TEST_END();
}

static int test_muninn_reinforce_contradict(void) {
    TEST_BEGIN();
    muninn_t m = {0};
    char path[256];
    make_temp_db(path, sizeof(path));
    ASSERT_EQ(muninn_init(&m, path), 0);
    char id[MUNINN_ID_MAX];
    muninn_write(&m, "default", "fact", "Earth is round", NULL, 0, id, sizeof(id));
    ASSERT_EQ(muninn_reinforce(&m, id, 0.1f), 0);
    ASSERT_EQ(muninn_contradict(&m, id), 0);
    muninn_engram_t e = {0};
    ASSERT_EQ(muninn_read(&m, "default", id, &e), 0);
    ASSERT_TRUE(e.confidence >= 0.0f && e.confidence <= 1.0f);
    muninn_free(&m);
    unlink(path);
    TEST_END();
}

static int test_muninn_soft_delete(void) {
    TEST_BEGIN();
    muninn_t m = {0};
    char path[256];
    make_temp_db(path, sizeof(path));
    ASSERT_EQ(muninn_init(&m, path), 0);
    char id[MUNINN_ID_MAX];
    muninn_write(&m, "v1", "x", "y", NULL, 0, id, sizeof(id));
    ASSERT_EQ(muninn_soft_delete(&m, "v1", id), 0);
    muninn_engram_t e = {0};
    ASSERT_EQ(muninn_read(&m, "v1", id, &e), -1);
    muninn_free(&m);
    unlink(path);
    TEST_END();
}

static int test_muninn_list_vaults(void) {
    TEST_BEGIN();
    muninn_t m = {0};
    char path[256];
    make_temp_db(path, sizeof(path));
    ASSERT_EQ(muninn_init(&m, path), 0);
    char id[MUNINN_ID_MAX];
    muninn_write(&m, "vault_a", "k", "v", NULL, 0, id, sizeof(id));
    muninn_write(&m, "vault_b", "k2", "v2", NULL, 0, id, sizeof(id));
    char vaults[4][MUNINN_VAULT_MAX];
    size_t count = 0;
    ASSERT_EQ(muninn_list_vaults(&m, vaults, 4, &count), 0);
    ASSERT_GE(count, 2u);
    muninn_free(&m);
    unlink(path);
    TEST_END();
}

static int test_muninn_init_null(void) {
    TEST_BEGIN();
    ASSERT_EQ(muninn_init(NULL, "/tmp/x.db"), -1);
    muninn_t m = {0};
    ASSERT_EQ(muninn_init(&m, NULL), -1);
    TEST_END();
}

int test_muninn_run(void) {
    int failed = 0;
    printf("  muninn: init_free\n");
    if (test_muninn_init_free() != 0) failed++;
    printf("  muninn: write_read\n");
    if (test_muninn_write_read() != 0) failed++;
    printf("  muninn: activate\n");
    if (test_muninn_activate() != 0) failed++;
    printf("  muninn: record_access\n");
    if (test_muninn_record_access() != 0) failed++;
    printf("  muninn: reinforce_contradict\n");
    if (test_muninn_reinforce_contradict() != 0) failed++;
    printf("  muninn: soft_delete\n");
    if (test_muninn_soft_delete() != 0) failed++;
    printf("  muninn: list_vaults\n");
    if (test_muninn_list_vaults() != 0) failed++;
    printf("  muninn: init_null\n");
    if (test_muninn_init_null() != 0) failed++;
    return failed;
}
