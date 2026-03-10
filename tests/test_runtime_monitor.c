#include "runtime_monitor.h"
#include "test_harness.h"
#include <stdio.h>
#include <string.h>

static int test_runtime_monitor_init_shutdown(void) {
    TEST_BEGIN();
    int r = runtime_monitor_init();
    ASSERT_EQ(r, 0);
    runtime_monitor_event(RUNTIME_EVENT_OTHER, "test", "127.0.0.1", "startup test");
    runtime_monitor_shutdown();
    TEST_END();
}

int test_runtime_monitor_run(void) {
    int failed = 0;
    printf("  runtime_monitor: init/shutdown\n");
    if (test_runtime_monitor_init_shutdown() != 0) failed++;
    return failed;
}
