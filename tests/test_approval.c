#include "approval.h"
#include "test_harness.h"
#include <stdio.h>
#include <string.h>

static int test_approval_init_free(void) {
    TEST_BEGIN();
    approval_manager_t mgr = {0};
    int r = approval_manager_init(&mgr);
    ASSERT_EQ(r, 0);
    approval_manager_free(&mgr);
    TEST_END();
}

static int test_approval_request_respond(void) {
    TEST_BEGIN();
    approval_manager_t mgr = {0};
    int r = approval_manager_init(&mgr);
    ASSERT_EQ(r, 0);
    approval_request_t req = {0};
    r = approval_request(&mgr, "shell", "test reason", &req);
    ASSERT_EQ(r, 0);
    ASSERT_TRUE(strlen(req.request_id) > 0);
    ASSERT_STR_EQ(req.action, "shell");
    r = approval_respond(&mgr, req.request_id, true);
    ASSERT_EQ(r, 0);
    approval_manager_free(&mgr);
    TEST_END();
}

static int test_approval_auto_approve(void) {
    TEST_BEGIN();
    approval_manager_t mgr = {0};
    approval_manager_init(&mgr);
    mgr.auto_approve_enabled = true;
    int check = approval_check(&mgr, "shell");
    ASSERT_EQ(check, 1);
    approval_manager_free(&mgr);
    TEST_END();
}

static int test_approval_denied(void) {
    TEST_BEGIN();
    approval_manager_t mgr = {0};
    approval_manager_init(&mgr);
    approval_request_t req = {0};
    approval_request(&mgr, "write", "test", &req);
    approval_respond(&mgr, req.request_id, false);
    int check = approval_check(&mgr, "write");
    ASSERT_EQ(check, 0);
    approval_manager_free(&mgr);
    TEST_END();
}

int test_approval_run(void) {
    int failed = 0;
    printf("  approval: init/free\n");
    if (test_approval_init_free() != 0) failed++;
    printf("  approval: request/respond\n");
    if (test_approval_request_respond() != 0) failed++;
    printf("  approval: auto_approve check\n");
    if (test_approval_auto_approve() != 0) failed++;
    printf("  approval: denied\n");
    if (test_approval_denied() != 0) failed++;
    return failed;
}
