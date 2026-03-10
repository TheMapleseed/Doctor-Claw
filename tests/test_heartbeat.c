#include "heartbeat.h"
#include "test_harness.h"
#include <stdio.h>
#include <string.h>

static int test_heartbeat_init_ping(void) {
    TEST_BEGIN();
    heartbeat_t hb = {0};
    int r = heartbeat_init(&hb, 1000);
    ASSERT_EQ(r, 0);
    ASSERT_TRUE(hb.interval_ms == 1000);
    r = heartbeat_ping(&hb);
    ASSERT_EQ(r, 0);
    bool alive = heartbeat_is_alive(&hb);
    ASSERT_TRUE(alive);
    heartbeat_stop(&hb);
    TEST_END();
}

int test_heartbeat_run(void) {
    int failed = 0;
    printf("  heartbeat: init/ping/stop\n");
    if (test_heartbeat_init_ping() != 0) failed++;
    return failed;
}
