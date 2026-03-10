#include "service.h"
#include "test_harness.h"
#include <stdio.h>
#include <string.h>

static int test_service_manager_init_free(void) {
    TEST_BEGIN();
    service_manager_t mgr = {0};
    int r = service_manager_init(&mgr);
    ASSERT_EQ(r, 0);
    service_manager_free(&mgr);
    TEST_END();
}

static int test_service_state_name(void) {
    TEST_BEGIN();
    const char *n = service_state_name(SERVICE_STATE_RUNNING);
    ASSERT_NOT_NULL(n);
    ASSERT_TRUE(strlen(n) > 0);
    TEST_END();
}

int test_service_run(void) {
    int failed = 0;
    printf("  service: init/free\n");
    if (test_service_manager_init_free() != 0) failed++;
    printf("  service: state_name\n");
    if (test_service_state_name() != 0) failed++;
    return failed;
}
