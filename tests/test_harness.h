#ifndef DOCTORCLAW_TEST_HARNESS_H
#define DOCTORCLAW_TEST_HARNESS_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern int g_test_failed;
extern int g_test_run;

#define TEST_BEGIN() do { g_test_run++; g_test_failed = 0; } while(0)
#define TEST_END()   do { if (g_test_failed) return -1; return 0; } while(0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "  FAIL %s:%d: ASSERT_TRUE(%s)\n", __FILE__, __LINE__, #cond); \
        g_test_failed = 1; \
        return -1; \
    } \
} while(0)

#define ASSERT_FALSE(cond) do { \
    if (cond) { \
        fprintf(stderr, "  FAIL %s:%d: ASSERT_FALSE(%s)\n", __FILE__, __LINE__, #cond); \
        g_test_failed = 1; \
        return -1; \
    } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        fprintf(stderr, "  FAIL %s:%d: %s (%d) != %s (%d)\n", __FILE__, __LINE__, #a, (int)(a), #b, (int)(b)); \
        g_test_failed = 1; \
        return -1; \
    } \
} while(0)

#define ASSERT_EQ_LL(a, b) do { \
    if ((long long)(a) != (long long)(b)) { \
        fprintf(stderr, "  FAIL %s:%d: %s != %s\n", __FILE__, __LINE__, #a, #b); \
        g_test_failed = 1; \
        return -1; \
    } \
} while(0)

#define ASSERT_STR_EQ(a, b) do { \
    if (strcmp((a), (b)) != 0) { \
        fprintf(stderr, "  FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); \
        g_test_failed = 1; \
        return -1; \
    } \
} while(0)

#define ASSERT_NULL(p) do { \
    if ((p) != NULL) { \
        fprintf(stderr, "  FAIL %s:%d: %s is not NULL\n", __FILE__, __LINE__, #p); \
        g_test_failed = 1; \
        return -1; \
    } \
} while(0)

#define ASSERT_NOT_NULL(p) do { \
    if ((p) == NULL) { \
        fprintf(stderr, "  FAIL %s:%d: %s is NULL\n", __FILE__, __LINE__, #p); \
        g_test_failed = 1; \
        return -1; \
    } \
} while(0)

#define ASSERT_GE(a, b) do { \
    if ((a) < (b)) { \
        fprintf(stderr, "  FAIL %s:%d: %s (%d) < %s (%d)\n", __FILE__, __LINE__, #a, (int)(a), #b, (int)(b)); \
        g_test_failed = 1; \
        return -1; \
    } \
} while(0)

#endif
