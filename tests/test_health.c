#include "health.h"
#include "test_harness.h"
#include <stdio.h>
#include <string.h>

static health_status_t dummy_check(void) { return HEALTH_OK; }

static int test_health_init_register(void) {
    TEST_BEGIN();
    health_monitor_t mon = {0};
    int r = health_init(&mon);
    ASSERT_EQ(r, 0);
    ASSERT_EQ(mon.check_count, (size_t)0);
    r = health_register(&mon, "test_component");
    ASSERT_EQ(r, 0);
    ASSERT_GE(mon.check_count, (size_t)1);
    health_free(&mon);
    TEST_END();
}

static int test_health_update_get(void) {
    TEST_BEGIN();
    health_monitor_t mon = {0};
    ASSERT_EQ(health_init(&mon), 0);
    ASSERT_EQ(health_register(&mon, "api"), 0);
    ASSERT_EQ(health_update(&mon, "api", HEALTH_OK, "ok"), 0);
    health_check_t out = {0};
    ASSERT_EQ(health_get(&mon, "api", &out), 0);
    ASSERT_EQ(out.status, HEALTH_OK);
    ASSERT_STR_EQ(out.component, "api");
    health_free(&mon);
    TEST_END();
}

static int test_health_run_checks(void) {
    TEST_BEGIN();
    health_monitor_t mon = {0};
    ASSERT_EQ(health_init(&mon), 0);
    int r = health_run_checks(&mon);
    ASSERT_EQ(r, 0);
    health_free(&mon);
    TEST_END();
}

static int test_health_register_checker(void) {
    TEST_BEGIN();
    health_monitor_t mon = {0};
    ASSERT_EQ(health_init(&mon), 0);
    int r = health_register_checker(&mon, "dummy", dummy_check);
    ASSERT_EQ(r, 0);
    health_free(&mon);
    TEST_END();
}

static int test_health_check_provider(void) {
    TEST_BEGIN();
    health_status_t s = health_check_provider();
    ASSERT_TRUE(s == HEALTH_OK || s == HEALTH_DEGRADED || s == HEALTH_FAILED);
    TEST_END();
}

static int test_health_check_memory(void) {
    TEST_BEGIN();
    health_status_t s = health_check_memory();
    ASSERT_TRUE(s == HEALTH_OK || s == HEALTH_DEGRADED || s == HEALTH_FAILED);
    TEST_END();
}

int test_health_run(void) {
    int failed = 0;
    printf("  health: init_register\n");
    if (test_health_init_register() != 0) failed++;
    printf("  health: update_get\n");
    if (test_health_update_get() != 0) failed++;
    printf("  health: run_checks\n");
    if (test_health_run_checks() != 0) failed++;
    printf("  health: register_checker\n");
    if (test_health_register_checker() != 0) failed++;
    printf("  health: check_provider\n");
    if (test_health_check_provider() != 0) failed++;
    printf("  health: check_memory\n");
    if (test_health_check_memory() != 0) failed++;
    return failed;
}
