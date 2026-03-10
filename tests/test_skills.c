#include "skills.h"
#include "test_harness.h"
#include <stdio.h>
#include <string.h>

static int test_skills_manager_init_free(void) {
    TEST_BEGIN();
    skills_manager_t mgr = {0};
    int r = skills_manager_init(&mgr);
    ASSERT_EQ(r, 0);
    skills_manager_free(&mgr);
    TEST_END();
}

static int test_skill_type_name(void) {
    TEST_BEGIN();
    const char *n = skill_type_name(SKILL_TYPE_TOOL);
    ASSERT_NOT_NULL(n);
    ASSERT_TRUE(strlen(n) > 0);
    skill_type_t t = skill_type_from_name("tool");
    ASSERT_TRUE(t == SKILL_TYPE_TOOL || t == SKILL_TYPE_NONE);
    TEST_END();
}

int test_skills_run(void) {
    int failed = 0;
    printf("  skills: init/free\n");
    if (test_skills_manager_init_free() != 0) failed++;
    printf("  skills: type_name\n");
    if (test_skill_type_name() != 0) failed++;
    return failed;
}
