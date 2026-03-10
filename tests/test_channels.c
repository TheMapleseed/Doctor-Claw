#include "channels.h"
#include "test_harness.h"
#include <stdio.h>
#include <string.h>

static int test_channel_get_type_name(void) {
    TEST_BEGIN();
    channel_type_t t = channel_get_type("telegram");
    ASSERT_TRUE(t == CHANNEL_TELEGRAM || t == CHANNEL_UNKNOWN);
    const char *n = channel_get_name(CHANNEL_TELEGRAM);
    ASSERT_NOT_NULL(n);
    ASSERT_TRUE(strlen(n) > 0);
    TEST_END();
}

static int test_channel_init_disconnect(void) {
    TEST_BEGIN();
    channel_t ch = {0};
    channel_config_t cfg = {0};
    snprintf(cfg.name, sizeof(cfg.name), "test");
    cfg.type = CHANNEL_CLI;
    cfg.enabled = true;
    int r = channel_init(&ch, CHANNEL_CLI, &cfg);
    ASSERT_EQ(r, 0);
    channel_disconnect(&ch);
    TEST_END();
}

int test_channels_run(void) {
    int failed = 0;
    printf("  channels: get_type/get_name\n");
    if (test_channel_get_type_name() != 0) failed++;
    printf("  channels: init/disconnect\n");
    if (test_channel_init_disconnect() != 0) failed++;
    return failed;
}
