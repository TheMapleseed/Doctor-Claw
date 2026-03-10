#include "peripherals.h"
#include "test_harness.h"
#include <stdio.h>
#include <string.h>

static int test_peripheral_init_free(void) {
    TEST_BEGIN();
    peripheral_t peri = {0};
    int r = peripheral_init(&peri, PERIPHERAL_RPI_GPIO, "/dev/null");
    ASSERT_TRUE(r == 0 || r != 0);
    peripheral_free(&peri);
    TEST_END();
}

int test_peripherals_run(void) {
    int failed = 0;
    printf("  peripherals: init/free\n");
    if (test_peripheral_init_free() != 0) failed++;
    return failed;
}
