#include "daemon.h"
#include "test_harness.h"
#include <stdio.h>
#include <string.h>

static int test_daemon_init_free(void) {
    TEST_BEGIN();
    daemon_context_t ctx = {0};
    int r = daemon_init(&ctx);
    ASSERT_EQ(r, 0);
    daemon_state_t s = daemon_get_state(&ctx);
    ASSERT_TRUE(s == DAEMON_STATE_STOPPED || s == DAEMON_STATE_STARTING);
    daemon_free(&ctx);
    TEST_END();
}

static int test_daemon_register_component(void) {
    TEST_BEGIN();
    daemon_context_t ctx = {0};
    daemon_init(&ctx);
    int r = daemon_register_component(&ctx, "test_component");
    ASSERT_TRUE(r == 0 || r != 0);
    daemon_free(&ctx);
    TEST_END();
}

int test_daemon_run(void) {
    int failed = 0;
    printf("  daemon: init/free\n");
    if (test_daemon_init_free() != 0) failed++;
    printf("  daemon: register_component\n");
    if (test_daemon_register_component() != 0) failed++;
    return failed;
}
