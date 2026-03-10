#include "instance.h"
#include "config.h"
#include "test_harness.h"
#include <stdio.h>
#include <string.h>

static int test_instance_register_get(void) {
    TEST_BEGIN();
    config_t cfg = {0};
    config_init_defaults(&cfg);
    snprintf(cfg.provider.provider, sizeof(cfg.provider.provider), "openrouter");
    instance_init();
    int r = instance_register("default", &cfg);
    ASSERT_EQ(r, 0);
    config_t out = {0};
    r = instance_get_config("default", &out);
    ASSERT_EQ(r, 0);
    ASSERT_STR_EQ(out.provider.provider, "openrouter");
    r = instance_get_config("nonexistent", &out);
    ASSERT_EQ(r, -1);
    instance_shutdown();
    r = instance_get_config("default", &out);
    ASSERT_EQ(r, -1);
    TEST_END();
}

int test_instance_run(void) {
    int failed = 0;
    printf("  instance: register/get/shutdown\n");
    if (test_instance_register_get() != 0) failed++;
    return failed;
}
