#include "cron.h"
#include "test_harness.h"
#include <stdio.h>
#include <string.h>

static int test_cron_init_shutdown(void) {
    TEST_BEGIN();
    int r = cron_init();
    ASSERT_EQ(r, 0);
    cron_shutdown();
    TEST_END();
}

static int test_cron_add_remove(void) {
    TEST_BEGIN();
    int r = cron_init();
    ASSERT_EQ(r, 0);
    r = cron_add_task("test_job_1", "* * * * *", "echo hello");
    ASSERT_EQ(r, 0);
    r = cron_remove_task("test_job_1");
    ASSERT_EQ(r, 0);
    r = cron_remove_task("nonexistent");
    /* remove of nonexistent may return 0 or -1 depending on impl */
    cron_shutdown();
    TEST_END();
}

static int test_cron_run_pending(void) {
    TEST_BEGIN();
    int r = cron_init();
    ASSERT_EQ(r, 0);
    cron_run_pending();
    cron_shutdown();
    TEST_END();
}

int test_cron_run(void) {
    int failed = 0;
    printf("  cron: init/shutdown\n");
    if (test_cron_init_shutdown() != 0) failed++;
    printf("  cron: add/remove task\n");
    if (test_cron_add_remove() != 0) failed++;
    printf("  cron: run_pending\n");
    if (test_cron_run_pending() != 0) failed++;
    return failed;
}
