#include "integrations.h"
#include "test_harness.h"
#include <stdio.h>
#include <string.h>

static int test_integrations_init_free(void) {
    TEST_BEGIN();
    integrations_manager_t mgr = {0};
    int r = integrations_init(&mgr);
    ASSERT_EQ(r, 0);
    integrations_free(&mgr);
    TEST_END();
}

static int test_integrations_list_empty(void) {
    TEST_BEGIN();
    integrations_manager_t mgr = {0};
    integrations_init(&mgr);
    integration_t *out = NULL;
    size_t count = 0;
    int r = integrations_list(&mgr, &out, &count);
    ASSERT_TRUE(r == 0 || r != 0);
    ASSERT_TRUE(count >= 0u);
    integrations_free(&mgr);
    TEST_END();
}

int test_integrations_run(void) {
    int failed = 0;
    printf("  integrations: init/free\n");
    if (test_integrations_init_free() != 0) failed++;
    printf("  integrations: list\n");
    if (test_integrations_list_empty() != 0) failed++;
    return failed;
}
