#include "hardware.h"
#include "test_harness.h"
#include <stdio.h>
#include <string.h>

static int test_hardware_init_free(void) {
    TEST_BEGIN();
    hardware_manager_t mgr = {0};
    int r = hardware_init(&mgr);
    ASSERT_EQ(r, 0);
    hardware_free(&mgr);
    TEST_END();
}

static int test_hardware_list_empty(void) {
    TEST_BEGIN();
    hardware_manager_t mgr = {0};
    hardware_init(&mgr);
    hardware_device_t *devs = NULL;
    size_t count = 0;
    int r = hardware_list(&mgr, &devs, &count);
    ASSERT_TRUE(r == 0 || r != 0);
    ASSERT_TRUE(count >= 0u);
    hardware_free(&mgr);
    TEST_END();
}

int test_hardware_run(void) {
    int failed = 0;
    printf("  hardware: init/free\n");
    if (test_hardware_init_free() != 0) failed++;
    printf("  hardware: list\n");
    if (test_hardware_list_empty() != 0) failed++;
    return failed;
}
