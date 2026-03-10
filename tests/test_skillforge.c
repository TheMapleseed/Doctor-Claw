#include "skillforge.h"
#include "test_harness.h"
#include <stdio.h>
#include <string.h>

static int test_skillforge_config_init_free(void) {
    TEST_BEGIN();
    skillforge_config_t cfg = {0};
    skillforge_config_init(&cfg);
    skillforge_config_free(&cfg);
    TEST_END();
}

static int test_skillforge_init_free(void) {
    TEST_BEGIN();
    skillforge_config_t cfg = {0};
    skillforge_config_init(&cfg);
    skillforge_t sf = {0};
    skillforge_init(&sf, &cfg);
    skillforge_free(&sf);
    skillforge_config_free(&cfg);
    TEST_END();
}

int test_skillforge_run(void) {
    int failed = 0;
    printf("  skillforge: config init/free\n");
    if (test_skillforge_config_init_free() != 0) failed++;
    printf("  skillforge: init/free\n");
    if (test_skillforge_init_free() != 0) failed++;
    return failed;
}
