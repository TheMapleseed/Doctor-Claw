#include "providers.h"
#include "test_harness.h"
#include <stdio.h>
#include <string.h>

static int test_providers_init_shutdown(void) {
    TEST_BEGIN();
    int r = providers_init();
    ASSERT_EQ(r, 0);
    providers_shutdown();
    TEST_END();
}

static int test_provider_get_name(void) {
    TEST_BEGIN();
    const char *n = provider_get_name(PROVIDER_OPENAI);
    ASSERT_NOT_NULL(n);
    ASSERT_TRUE(strlen(n) > 0);
    TEST_END();
}

static int test_chat_context_init_free(void) {
    TEST_BEGIN();
    chat_context_t ctx = {0};
    chat_context_init(&ctx);
    chat_context_add_message(&ctx, "user", "hello");
    chat_context_clear_messages(&ctx);
    chat_context_free(&ctx);
    TEST_END();
}

int test_providers_run(void) {
    int failed = 0;
    printf("  providers: init/shutdown\n");
    if (test_providers_init_shutdown() != 0) failed++;
    printf("  providers: get_name\n");
    if (test_provider_get_name() != 0) failed++;
    printf("  providers: chat_context\n");
    if (test_chat_context_init_free() != 0) failed++;
    return failed;
}
