#include "tools.h"
#include "test_harness.h"
#include <stdio.h>
#include <string.h>

static int test_tools_list(void) {
    TEST_BEGIN();
    tool_spec_t *tools = NULL;
    size_t count = 0;
    tools_list(&tools, &count);
    ASSERT_NOT_NULL(tools);
    ASSERT_GE(count, 10);
    ASSERT_STR_EQ(tools[0].name, "shell");
    ASSERT_TRUE(strlen(tools[0].description) > 0);
    /* list and env are in the static list */
    int has_list = 0, has_env = 0;
    for (size_t i = 0; i < count; i++) {
        if (strcmp(tools[i].name, "list") == 0) has_list = 1;
        if (strcmp(tools[i].name, "env") == 0) has_env = 1;
    }
    ASSERT_TRUE(has_list);
    ASSERT_TRUE(has_env);
    TEST_END();
}

static int test_tools_execute_list(void) {
    TEST_BEGIN();
    tools_init();
    tool_result_t result = {0};
    /* cron_list has no side effects and needs no workspace */
    int r = tools_execute("cron_list", "{}", &result);
    tools_shutdown();
    ASSERT_EQ(r, 0);
    ASSERT_TRUE(result.success);
    TEST_END();
}

static int test_tools_execute_env(void) {
    TEST_BEGIN();
    tools_init();
    tool_result_t result = {0};
    int r = tools_execute("env", "{\"name\": \"PATH\"}", &result);
    tools_shutdown();
    ASSERT_EQ(r, 0);
    ASSERT_TRUE(result.success);
    TEST_END();
}

static int test_tools_execute_unknown(void) {
    TEST_BEGIN();
    tools_init();
    tool_result_t result = {0};
    int r = tools_execute("nonexistent_tool_xyz", "{}", &result);
    tools_shutdown();
    ASSERT_EQ(r, -1);
    ASSERT_FALSE(result.success);
    TEST_END();
}

int test_tools_run(void) {
    int failed = 0;
    printf("  tools: list\n");
    if (test_tools_list() != 0) failed++;
    printf("  tools: execute list\n");
    if (test_tools_execute_list() != 0) failed++;
    printf("  tools: execute env\n");
    if (test_tools_execute_env() != 0) failed++;
    printf("  tools: execute unknown\n");
    if (test_tools_execute_unknown() != 0) failed++;
    return failed;
}
