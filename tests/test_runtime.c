#include "runtime.h"
#include "test_harness.h"
#include <stdio.h>
#include <string.h>

static int test_runtime_init_get_info(void) {
    TEST_BEGIN();
    int r = runtime_init();
    ASSERT_EQ(r, 0);
    runtime_info_t info = {0};
    r = runtime_get_info(&info);
    ASSERT_EQ(r, 0);
    ASSERT_TRUE(info.state == RUNTIME_STATE_IDLE || info.state == RUNTIME_STATE_RUNNING);
    TEST_END();
}

int test_runtime_run(void) {
    int failed = 0;
    printf("  runtime: init/get_info\n");
    if (test_runtime_init_get_info() != 0) failed++;
    return failed;
}
