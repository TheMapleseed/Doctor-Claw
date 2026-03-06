#include "config.h"
#include "test_harness.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int test_config_init_defaults(void) {
    TEST_BEGIN();
    config_t cfg;
    config_init_defaults(&cfg);
    ASSERT_TRUE(strlen(cfg.paths.workspace_dir) > 0);
    ASSERT_STR_EQ(cfg.provider.provider, "openrouter");
    ASSERT_TRUE(cfg.provider.temperature > 0.0);
    ASSERT_EQ(cfg.gateway.port, 8080);
    ASSERT_STR_EQ(cfg.autonomy.level_str, "supervised");
    TEST_END();
}

static int test_config_load_null_path_uses_defaults(void) {
    TEST_BEGIN();
    config_t cfg;
    int r = config_load(NULL, &cfg);
    ASSERT_EQ(r, 0);
    ASSERT_TRUE(strlen(cfg.paths.workspace_dir) > 0);
    ASSERT_TRUE(strlen(cfg.paths.config_path) > 0);
    TEST_END();
}

static int test_config_load_from_file(void) {
    TEST_BEGIN();
    char path[512];
    if (!getcwd(path, sizeof(path) - 64)) {
        fprintf(stderr, "  SKIP: getcwd failed\n");
        return 0;
    }
    size_t len = strlen(path);
    snprintf(path + len, sizeof(path) - len, "/doctorclaw_test_config.ini");
    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "  SKIP: cannot create %s\n", path);
        return 0;
    }
    fprintf(f, "[paths]\nworkspace_dir = /tmp/doctorclaw_ws\n");
    fprintf(f, "[provider]\nprovider = openai\nmodel = gpt-4\n");
    fprintf(f, "[gateway]\nport = 9090\n");
    fclose(f);

    config_t cfg;
    int r = config_load(path, &cfg);
    ASSERT_EQ(r, 0);
    ASSERT_STR_EQ(cfg.provider.provider, "openai");
    ASSERT_STR_EQ(cfg.provider.model, "gpt-4");
    ASSERT_EQ(cfg.gateway.port, 9090);
    ASSERT_TRUE(strlen(cfg.paths.config_path) > 0);
    /* workspace_dir from file unless env DOCTORCLAW_WORKSPACE overrides */
    if (!getenv("DOCTORCLAW_WORKSPACE"))
        ASSERT_STR_EQ(cfg.paths.workspace_dir, "/tmp/doctorclaw_ws");

    unlink(path);
    TEST_END();
}

int test_config_run(void) {
    int failed = 0;
    printf("  config: init_defaults\n");
    if (test_config_init_defaults() != 0) failed++;
    printf("  config: load NULL path\n");
    if (test_config_load_null_path_uses_defaults() != 0) failed++;
    printf("  config: load from file\n");
    if (test_config_load_from_file() != 0) failed++;
    return failed;
}
