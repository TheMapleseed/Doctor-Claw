#include "auth.h"
#include "test_harness.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int test_auth_init_free(void) {
    TEST_BEGIN();
    auth_t auth = {0};
    int r = auth_init(&auth);
    ASSERT_EQ(r, 0);
    ASSERT_EQ(auth.profile_count, 0);
    auth_free(&auth);
    TEST_END();
}

static int test_auth_add_remove_profile(void) {
    TEST_BEGIN();
    auth_t auth = {0};
    auth_init(&auth);
    int r = auth_add_profile(&auth, "test_profile", AUTH_PROVIDER_OPENAI, "sk-test-key");
    ASSERT_EQ(r, 0);
    ASSERT_EQ(auth.profile_count, 1);
    ASSERT_STR_EQ(auth.profiles[0].name, "test_profile");
    ASSERT_STR_EQ(auth.profiles[0].api_key, "sk-test-key");
    r = auth_remove_profile(&auth, "test_profile");
    ASSERT_EQ(r, 0);
    ASSERT_EQ(auth.profile_count, 0);
    auth_free(&auth);
    TEST_END();
}

static int test_auth_set_active_get_active(void) {
    TEST_BEGIN();
    auth_t auth = {0};
    auth_init(&auth);
    auth_add_profile(&auth, "p1", AUTH_PROVIDER_OPENAI, "key1");
    auth_add_profile(&auth, "p2", AUTH_PROVIDER_ANTHROPIC, "key2");
    int r = auth_set_active(&auth, "p2");
    ASSERT_EQ(r, 0);
    auth_profile_t active = {0};
    r = auth_get_active(&auth, &active);
    ASSERT_EQ(r, 0);
    ASSERT_STR_EQ(active.name, "p2");
    ASSERT_STR_EQ(active.api_key, "key2");
    auth_free(&auth);
    TEST_END();
}

static int test_auth_provider_name(void) {
    TEST_BEGIN();
    ASSERT_NOT_NULL(auth_provider_name(AUTH_PROVIDER_OPENAI));
    ASSERT_STR_EQ(auth_provider_name(AUTH_PROVIDER_OPENAI), "openai");
    ASSERT_EQ(auth_provider_from_name("openai"), AUTH_PROVIDER_OPENAI);
    ASSERT_EQ(auth_provider_from_name("anthropic"), AUTH_PROVIDER_ANTHROPIC);
    TEST_END();
}

static int test_auth_save_load(void) {
    TEST_BEGIN();
    char path[512];
    if (!getcwd(path, sizeof(path) - 32)) return 0;
    size_t len = strlen(path);
    snprintf(path + len, sizeof(path) - len, "/doctorclaw_test_auth.txt");
    auth_t auth = {0};
    auth_init(&auth);
    auth_add_profile(&auth, "saved", AUTH_PROVIDER_OPENROUTER, "saved-key");
    auth_set_active(&auth, "saved");
    int r = auth_save(&auth, path);
    ASSERT_EQ(r, 0);
    auth_t loaded = {0};
    auth_init(&loaded);
    r = auth_load(&loaded, path);
    ASSERT_EQ(r, 0);
    auth_free(&auth);
    auth_free(&loaded);
    unlink(path);
    TEST_END();
}

int test_auth_run(void) {
    int failed = 0;
    printf("  auth: init/free\n");
    if (test_auth_init_free() != 0) failed++;
    printf("  auth: add/remove profile\n");
    if (test_auth_add_remove_profile() != 0) failed++;
    printf("  auth: set_active/get_active\n");
    if (test_auth_set_active_get_active() != 0) failed++;
    printf("  auth: provider name\n");
    if (test_auth_provider_name() != 0) failed++;
    printf("  auth: save/load\n");
    if (test_auth_save_load() != 0) failed++;
    return failed;
}
