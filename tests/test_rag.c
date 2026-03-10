#include "rag.h"
#include "test_harness.h"
#include <stdio.h>
#include <string.h>

static int test_rag_index_init_free(void) {
    TEST_BEGIN();
    rag_index_t idx = {0};
    int r = rag_index_init(&idx);
    ASSERT_EQ(r, 0);
    rag_index_free(&idx);
    TEST_END();
}

static int test_rag_cosine_similarity(void) {
    TEST_BEGIN();
    double a[3] = {1.0, 0.0, 0.0};
    double b[3] = {1.0, 0.0, 0.0};
    double sim = rag_cosine_similarity(a, b, 3);
    ASSERT_TRUE(sim >= 0.99 && sim <= 1.01);
    TEST_END();
}

int test_rag_run(void) {
    int failed = 0;
    printf("  rag: index init/free\n");
    if (test_rag_index_init_free() != 0) failed++;
    printf("  rag: cosine_similarity\n");
    if (test_rag_cosine_similarity() != 0) failed++;
    return failed;
}
