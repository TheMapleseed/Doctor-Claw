#include "llama.h"
#include "test_harness.h"
#include <stdio.h>
#include <string.h>

static int test_llama_is_loaded_null(void) {
    TEST_BEGIN();
    /* NULL model: must not crash, is_loaded is false */
    ASSERT_FALSE(llama_is_loaded(NULL));
    TEST_END();
}

static int test_llama_get_model_name_null(void) {
    TEST_BEGIN();
    ASSERT_NULL(llama_get_model_name(NULL));
    TEST_END();
}

static int test_llama_unloaded_model(void) {
    TEST_BEGIN();
    llama_model_t model = {0};
    ASSERT_FALSE(llama_is_loaded(&model));
    ASSERT_NULL(llama_get_model_name(&model));
    TEST_END();
}

static int test_llama_init_optional(void) {
    TEST_BEGIN();
    llama_model_t model = {0};
    int r = llama_init(&model, NULL);
    /* Init may fail if libllama.dylib / libllama.so not installed; that's OK in CI */
    (void)r;
    ASSERT_TRUE(r == 0 || r == -1);
    if (r == 0) {
        ASSERT_FALSE(llama_is_loaded(&model)); /* still no model loaded */
    }
    TEST_END();
}

static int test_llama_load_invalid_optional(void) {
    TEST_BEGIN();
    llama_model_t model = {0};
    /* NULL path must fail without calling into the library (no hang on missing file) */
    int r = llama_load_model(&model, NULL);
    ASSERT_EQ(r, -1);
    ASSERT_FALSE(llama_is_loaded(&model));
    TEST_END();
}

int test_llama_run(void) {
    int failed = 0;
    printf("  llama: is_loaded(NULL)\n");
    if (test_llama_is_loaded_null() != 0) failed++;
    printf("  llama: get_model_name(NULL)\n");
    if (test_llama_get_model_name_null() != 0) failed++;
    printf("  llama: unloaded model\n");
    if (test_llama_unloaded_model() != 0) failed++;
    printf("  llama: init (optional lib)\n");
    if (test_llama_init_optional() != 0) failed++;
    printf("  llama: load invalid path (optional)\n");
    if (test_llama_load_invalid_optional() != 0) failed++;
    return failed;
}
