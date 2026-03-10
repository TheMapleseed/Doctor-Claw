#include "loop_guard.h"
#include "test_harness.h"
#include <stdio.h>
#include <string.h>

static int test_loop_guard_reset_check(void) {
    TEST_BEGIN();
    loop_guard_reset();
    bool ok = loop_guard_check("tool_a");
    ASSERT_TRUE(ok);
    ASSERT_FALSE(loop_guard_tripped());
    char buf[64] = {0};
    loop_guard_reason(buf, sizeof(buf));
    TEST_END();
}

int test_loop_guard_run(void) {
    int failed = 0;
    printf("  loop_guard: reset/check\n");
    if (test_loop_guard_reset_check() != 0) failed++;
    return failed;
}
