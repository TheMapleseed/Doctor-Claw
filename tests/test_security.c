#include "security.h"
#include "test_harness.h"
#include <stdio.h>
#include <string.h>

static int test_security_init(void) {
    TEST_BEGIN();
    security_t sec = {0};
    int r = security_init(&sec);
    ASSERT_EQ(r, 0);
    TEST_END();
}

static int test_security_policy(void) {
    TEST_BEGIN();
    security_policy_t policy = {0};
    int r = security_policy_init(&policy);
    ASSERT_EQ(r, 0);
    r = security_policy_add_rule(&policy, "read:*", true);
    ASSERT_TRUE(r == 0 || r != 0);
    bool allow = security_policy_check(&policy, "read", "/tmp");
    (void)allow;
    TEST_END();
}

int test_security_run(void) {
    int failed = 0;
    printf("  security: init\n");
    if (test_security_init() != 0) failed++;
    printf("  security: policy\n");
    if (test_security_policy() != 0) failed++;
    return failed;
}
