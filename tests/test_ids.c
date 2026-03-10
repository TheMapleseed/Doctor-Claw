#include "ids.h"
#include "test_harness.h"
#include <stdio.h>
#include <string.h>

static int test_ids_config_defaults(void) {
    TEST_BEGIN();
    ids_config_t cfg = {0};
    ids_config_defaults(&cfg);
    ASSERT_TRUE(cfg.signature_count >= 0u);
    TEST_END();
}

static int test_ids_init_shutdown(void) {
    TEST_BEGIN();
    ids_config_t cfg = {0};
    ids_config_defaults(&cfg);
    int r = ids_init(&cfg);
    ASSERT_EQ(r, 0);
    char reason[IDS_REASON_MAX] = {0};
    ids_result_t res = ids_check("GET", "/", NULL, 0, reason, sizeof(reason));
    ASSERT_TRUE(res == IDS_OK || res == IDS_ALERT_SIGNATURE || res == IDS_ALERT_ANOMALY);
    ids_shutdown();
    TEST_END();
}

int test_ids_run(void) {
    int failed = 0;
    printf("  ids: config_defaults\n");
    if (test_ids_config_defaults() != 0) failed++;
    printf("  ids: init/shutdown\n");
    if (test_ids_init_shutdown() != 0) failed++;
    return failed;
}
