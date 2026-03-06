#include "util.h"
#include "test_harness.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int test_util_trim(void) {
    TEST_BEGIN();
    /* util_trim_whitespace trims end and advances start but does not memmove */
    char s1[] = "hello  \t";
    util_trim_whitespace(s1);
    ASSERT_STR_EQ(s1, "hello");
    char s2[] = "x \n";
    util_trim_whitespace(s2);
    ASSERT_STR_EQ(s2, "x");
    TEST_END();
}

static int test_util_hex_encode_decode(void) {
    TEST_BEGIN();
    unsigned char bin[] = {0xde, 0xad, 0xbe, 0xef};
    char hex[32] = {0};
    int r = util_hex_encode(bin, 4, hex);
    ASSERT_EQ(r, 0);
    ASSERT_STR_EQ(hex, "deadbeef");
    unsigned char out[8];
    size_t out_len = 0;
    r = util_hex_decode(hex, out, &out_len);
    ASSERT_EQ(r, 0);
    ASSERT_EQ((int)out_len, 4);
    ASSERT_EQ(out[0], 0xde);
    ASSERT_EQ(out[3], 0xef);
    TEST_END();
}

static int test_util_strdup(void) {
    TEST_BEGIN();
    char *copy = util_strdup("hello");
    ASSERT_NOT_NULL(copy);
    ASSERT_STR_EQ(copy, "hello");
    free(copy);
    ASSERT_NULL(util_strdup(NULL));
    TEST_END();
}

static int test_util_base64(void) {
    TEST_BEGIN();
    unsigned char in[] = "Hi";
    char b64[16] = {0};
    int r = util_base64_encode(in, 2, b64, sizeof(b64));
    ASSERT_EQ(r, 0);
    ASSERT_TRUE(strlen(b64) >= 4);
    unsigned char dec[16];
    size_t dec_len = sizeof(dec);
    r = util_base64_decode(b64, dec, &dec_len);
    ASSERT_EQ(r, 0);
    ASSERT_TRUE(dec_len <= sizeof(dec));
    TEST_END();
}

int test_util_run(void) {
    int failed = 0;
    printf("  util: trim_whitespace\n");
    if (test_util_trim() != 0) failed++;
    printf("  util: hex_encode/decode\n");
    if (test_util_hex_encode_decode() != 0) failed++;
    printf("  util: strdup\n");
    if (test_util_strdup() != 0) failed++;
    printf("  util: base64\n");
    if (test_util_base64() != 0) failed++;
    return failed;
}
