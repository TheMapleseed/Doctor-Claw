#include "memory.h"
#include "test_harness.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static void make_temp_workspace(char *buf, size_t size) {
    snprintf(buf, size, "/tmp/doctorclaw_test_%d", (int)getpid());
    mkdir(buf, 0700);
}

static void remove_temp_workspace(const char *path) {
    char db_path[512];
    snprintf(db_path, sizeof(db_path), "%s/memory.db", path);
    unlink(db_path);
    rmdir(path);
}

static int test_memory_classify(void) {
    TEST_BEGIN();
    ASSERT_EQ(memory_classify("sqlite"), MEMORY_BACKEND_SQLITE);
    ASSERT_EQ(memory_classify("markdown"), MEMORY_BACKEND_MARKDOWN);
    ASSERT_EQ(memory_classify("none"), MEMORY_BACKEND_NONE);
    ASSERT_EQ(memory_classify("unknown_xyz"), MEMORY_BACKEND_UNKNOWN);
    TEST_END();
}

static int test_memory_create_store_recall_delete(void) {
    TEST_BEGIN();
    char workspace[256];
    make_temp_workspace(workspace, sizeof(workspace));

    memory_t mem = {0};
    int r = memory_create("sqlite", workspace, &mem);
    ASSERT_EQ(r, 0);
    ASSERT_TRUE(mem.initialized);

    memory_item_t item = {0};
    snprintf(item.key, sizeof(item.key), "test_key");
    snprintf(item.value, sizeof(item.value), "test_value");
    item.timestamp = 12345;

    r = memory_store(&mem, &item);
    ASSERT_EQ(r, 0);

    memory_item_t out = {0};
    r = memory_recall(&mem, "test_key", &out);
    ASSERT_EQ(r, 0);
    ASSERT_STR_EQ(out.key, "test_key");
    ASSERT_STR_EQ(out.value, "test_value");
    ASSERT_EQ_LL(out.timestamp, 12345);

    r = memory_recall(&mem, "nonexistent", &out);
    ASSERT_EQ(r, -1);

    r = memory_delete(&mem, "test_key");
    ASSERT_EQ(r, 0);

    r = memory_recall(&mem, "test_key", &out);
    ASSERT_EQ(r, -1);

    memory_free(&mem);
    remove_temp_workspace(workspace);
    TEST_END();
}

static int test_memory_create_invalid(void) {
    TEST_BEGIN();
    memory_t mem = {0};
    int r = memory_create(NULL, "/tmp", &mem);
    ASSERT_EQ(r, -1);
    r = memory_create("sqlite", NULL, &mem);
    ASSERT_EQ(r, -1);
    r = memory_create("sqlite", "/tmp", NULL);
    ASSERT_EQ(r, -1);
    TEST_END();
}

int test_memory_run(void) {
    int failed = 0;
    printf("  memory: classify\n");
    if (test_memory_classify() != 0) failed++;
    printf("  memory: create/store/recall/delete\n");
    if (test_memory_create_store_recall_delete() != 0) failed++;
    printf("  memory: create invalid args\n");
    if (test_memory_create_invalid() != 0) failed++;
    return failed;
}
