#include "identity.h"
#include "test_harness.h"
#include <stdio.h>
#include <string.h>

static int test_identity_init_free(void) {
    TEST_BEGIN();
    identity_t id = {0};
    int r = identity_init(&id);
    ASSERT_EQ(r, 0);
    ASSERT_FALSE(identity_is_initialized(&id));
    identity_free(&id);
    TEST_END();
}

int test_identity_run(void) {
    int failed = 0;
    printf("  identity: init/free\n");
    if (test_identity_init_free() != 0) failed++;
    return failed;
}
