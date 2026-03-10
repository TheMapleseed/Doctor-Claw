#include "jobcache.h"
#include "jobworker.h"
#include "config.h"
#include "test_harness.h"
#include <stdio.h>
#include <string.h>

static int test_jobworker_pool_create_destroy(void) {
    TEST_BEGIN();
    jobcache_t *cache = jobcache_create(1024);
    ASSERT_NOT_NULL(cache);
    config_t cfg = {0};
    config_init_defaults(&cfg);
    jobworker_pool_t *pool = jobworker_pool_create(cache, NULL, &cfg, 0, 2);
    ASSERT_NOT_NULL(pool);
    jobworker_pool_destroy(pool);
    jobcache_destroy(cache);
    TEST_END();
}

int test_jobworker_run(void) {
    int failed = 0;
    printf("  jobworker: pool create/destroy\n");
    if (test_jobworker_pool_create_destroy() != 0) failed++;
    return failed;
}
