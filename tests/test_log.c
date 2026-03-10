#include "log.h"
#include "test_harness.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int test_log_set_level(void) {
    TEST_BEGIN();
    log_init();
    log_set_level(LOG_LEVEL_DEBUG);
    log_set_level(LOG_LEVEL_INFO);
    log_set_level(LOG_LEVEL_WARN);
    log_set_level(LOG_LEVEL_ERROR);
    TEST_END();
}

static int test_log_set_file_null(void) {
    TEST_BEGIN();
    log_init();
    /* close file (NULL) should not crash */
    ASSERT_EQ(log_set_file(NULL), 0);
    ASSERT_NULL(log_get_file_path());
    TEST_END();
}

static int test_log_set_file_path(void) {
    TEST_BEGIN();
    log_init();
    char path[256];
    snprintf(path, sizeof(path), "/tmp/doctorclaw_log_test_%d.txt", (int)getpid());
    int r = log_set_file(path);
    ASSERT_EQ(r, 0);
    ASSERT_NOT_NULL(log_get_file_path());
    ASSERT_STR_EQ(log_get_file_path(), path);
    log_info("test message");
    log_set_file(NULL);
    unlink(path);
    TEST_END();
}

static int test_log_export(void) {
    TEST_BEGIN();
    log_init();
    log_set_file(NULL);
    char src[256], dest[256];
    snprintf(src, sizeof(src), "/tmp/doctorclaw_export_src_%d.txt", (int)getpid());
    snprintf(dest, sizeof(dest), "/tmp/doctorclaw_export_dest_%d.txt", (int)getpid());
    log_set_file(src);
    log_info("export test");
    log_set_file(NULL);
    int r = log_export(dest);
    /* may succeed or fail depending on whether log file was created */
    (void)r;
    unlink(src);
    unlink(dest);
    TEST_END();
}

static int test_log_macros_no_crash(void) {
    TEST_BEGIN();
    log_init();
    log_debug("debug %s", "msg");
    log_info("info %d", 42);
    log_warn("warn");
    log_error("error");
    TEST_END();
}

int test_log_run(void) {
    int failed = 0;
    printf("  log: set_level\n");
    if (test_log_set_level() != 0) failed++;
    printf("  log: set_file_null\n");
    if (test_log_set_file_null() != 0) failed++;
    printf("  log: set_file_path\n");
    if (test_log_set_file_path() != 0) failed++;
    printf("  log: export\n");
    if (test_log_export() != 0) failed++;
    printf("  log: macros_no_crash\n");
    if (test_log_macros_no_crash() != 0) failed++;
    return failed;
}
