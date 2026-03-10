#include "migration.h"
#include "test_harness.h"
#include <stdio.h>
#include <string.h>

static int test_migration_manager_init_free(void) {
    TEST_BEGIN();
    migration_manager_t mgr = {0};
    int r = migration_manager_init(&mgr);
    ASSERT_EQ(r, 0);
    migration_manager_free(&mgr);
    TEST_END();
}

static int test_migration_source_name(void) {
    TEST_BEGIN();
    const char *n = migration_source_name(MIGRATION_SOURCE_OPENAI);
    ASSERT_NOT_NULL(n);
    ASSERT_TRUE(strlen(n) > 0);
    migration_source_t t = migration_source_from_name("openai");
    ASSERT_TRUE(t == MIGRATION_SOURCE_OPENAI || t == MIGRATION_SOURCE_NONE);
    TEST_END();
}

int test_migration_run(void) {
    int failed = 0;
    printf("  migration: init/free\n");
    if (test_migration_manager_init_free() != 0) failed++;
    printf("  migration: source_name\n");
    if (test_migration_source_name() != 0) failed++;
    return failed;
}
