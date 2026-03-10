#include "gateway.h"
#include "test_harness.h"
#include <stdio.h>
#include <string.h>

static int test_ws_init_free(void) {
    TEST_BEGIN();
    ws_server_t ws = {0};
    int r = ws_init(&ws);
    ASSERT_EQ(r, 0);
    ASSERT_EQ(ws.client_count, 0u);
    ws_free(&ws);
    TEST_END();
}

int test_gateway_run(void) {
    int failed = 0;
    printf("  gateway: ws_init/ws_free\n");
    if (test_ws_init_free() != 0) failed++;
    return failed;
}
