#include "cost.h"
#include "test_harness.h"
#include <stdio.h>
#include <string.h>

static int test_cost_tracker_init_free(void) {
    TEST_BEGIN();
    cost_tracker_t tracker = {0};
    int r = cost_tracker_init(&tracker);
    ASSERT_EQ(r, 0);
    double total = 0.0;
    r = cost_get_total(&tracker, &total);
    ASSERT_EQ(r, 0);
    ASSERT_TRUE(total >= 0.0);
    cost_tracker_free(&tracker);
    TEST_END();
}

static int test_cost_track(void) {
    TEST_BEGIN();
    cost_tracker_t tracker = {0};
    cost_tracker_init(&tracker);
    int r = cost_track(&tracker, "openai", "gpt-4", 100, 50);
    ASSERT_EQ(r, 0);
    double total = 0.0;
    cost_get_total(&tracker, &total);
    ASSERT_TRUE(total >= 0.0);
    cost_tracker_free(&tracker);
    TEST_END();
}

int test_cost_run(void) {
    int failed = 0;
    printf("  cost: init/free\n");
    if (test_cost_tracker_init_free() != 0) failed++;
    printf("  cost: track\n");
    if (test_cost_track() != 0) failed++;
    return failed;
}
