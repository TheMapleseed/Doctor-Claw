#include "observability.h"
#include "test_harness.h"
#include <stdio.h>
#include <string.h>

static int test_observability_init_reset(void) {
    TEST_BEGIN();
    observability_t obs = {0};
    int r = observability_init(&obs);
    ASSERT_EQ(r, 0);
    ASSERT_EQ(obs.count, (size_t)0);
    observability_reset(&obs);
    TEST_END();
}

static int test_observability_record_get(void) {
    TEST_BEGIN();
    observability_t obs = {0};
    ASSERT_EQ(observability_init(&obs), 0);
    ASSERT_EQ(observability_record(&obs, "requests_total", 10.0), 0);
    ASSERT_EQ(observability_record(&obs, "requests_total", 5.0), 0);
    double v = 0;
    ASSERT_EQ(observability_get(&obs, "requests_total", &v), 0);
    ASSERT_TRUE(v >= 10.0 && v <= 15.0);
    observability_reset(&obs);
    TEST_END();
}

static int test_observability_prometheus_export(void) {
    TEST_BEGIN();
    observability_t obs = {0};
    ASSERT_EQ(observability_init(&obs), 0);
    observability_record(&obs, "test_metric", 1.0);
    char buf[4096];
    int r = observability_prometheus_export(&obs, buf, sizeof(buf));
    ASSERT_EQ(r, 0);
    ASSERT_TRUE(strlen(buf) > 0);
    ASSERT_TRUE(strstr(buf, "test_metric") != NULL || strstr(buf, "metric") != NULL);
    TEST_END();
}

static int test_observability_global(void) {
    TEST_BEGIN();
    observability_global_init();
    observability_t *g = observability_global_get();
    ASSERT_NOT_NULL(g);
    ASSERT_EQ(observability_record_global("global_counter", 1.0), 0);
    TEST_END();
}

int test_observability_run(void) {
    int failed = 0;
    printf("  observability: init_reset\n");
    if (test_observability_init_reset() != 0) failed++;
    printf("  observability: record_get\n");
    if (test_observability_record_get() != 0) failed++;
    printf("  observability: prometheus_export\n");
    if (test_observability_prometheus_export() != 0) failed++;
    printf("  observability: global\n");
    if (test_observability_global() != 0) failed++;
    return failed;
}
