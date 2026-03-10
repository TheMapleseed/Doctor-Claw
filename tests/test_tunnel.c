#include "tunnel.h"
#include "test_harness.h"
#include <stdio.h>
#include <string.h>

static int test_tunnel_init_disconnect(void) {
    TEST_BEGIN();
    tunnel_t t = {0};
    int r = tunnel_init(&t);
    ASSERT_EQ(r, 0);
    ASSERT_FALSE(tunnel_is_connected(&t));
    tunnel_disconnect(&t);
    TEST_END();
}

int test_tunnel_run(void) {
    int failed = 0;
    printf("  tunnel: init/disconnect\n");
    if (test_tunnel_init_disconnect() != 0) failed++;
    return failed;
}
