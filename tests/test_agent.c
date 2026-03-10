#include "agent.h"
#include "config.h"
#include "test_harness.h"
#include <stdio.h>
#include <string.h>

static int test_agent_init_free(void) {
    TEST_BEGIN();
    config_t cfg = {0};
    config_init_defaults(&cfg);
    config_load(NULL, &cfg);
    agent_t agent = {0};
    int r = agent_init(&agent, &cfg);
    ASSERT_EQ(r, 0);
    agent_state_t state = AGENT_STATE_IDLE;
    ASSERT_EQ(agent_get_state(&agent, &state), 0);
    agent_free(&agent);
    TEST_END();
}

static int test_agent_task_marker(void) {
    TEST_BEGIN();
    const char *m = agent_task_complete_marker();
    ASSERT_NOT_NULL(m);
    ASSERT_TRUE(strlen(m) > 0);
    TEST_END();
}

int test_agent_run(void) {
    int failed = 0;
    printf("  agent: init/free\n");
    if (test_agent_init_free() != 0) failed++;
    printf("  agent: task_complete_marker\n");
    if (test_agent_task_marker() != 0) failed++;
    return failed;
}
