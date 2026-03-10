#include "onboard.h"
#include "test_harness.h"
#include <stdio.h>
#include <string.h>

static int test_onboard_init_free(void) {
    TEST_BEGIN();
    onboard_t ob = {0};
    int r = onboard_init(&ob);
    ASSERT_EQ(r, 0);
    onboard_free(&ob);
    TEST_END();
}

static int test_onboard_state_name(void) {
    TEST_BEGIN();
    const char *n = onboard_state_name(ONBOARD_STATE_NOT_STARTED);
    ASSERT_NOT_NULL(n);
    ASSERT_TRUE(strlen(n) > 0);
    TEST_END();
}

int test_onboard_run(void) {
    int failed = 0;
    printf("  onboard: init/free\n");
    if (test_onboard_init_free() != 0) failed++;
    printf("  onboard: state_name\n");
    if (test_onboard_state_name() != 0) failed++;
    return failed;
}
