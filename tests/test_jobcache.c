#include "jobcache.h"
#include "test_harness.h"
#include <stdio.h>
#include <string.h>

static int test_jobcache_create_destroy(void) {
    TEST_BEGIN();
    jobcache_t *cache = jobcache_create(8);
    ASSERT_NOT_NULL(cache);
    ASSERT_EQ(jobcache_capacity(cache), (size_t)8);
    ASSERT_EQ(jobcache_depth(cache), (size_t)0);
    jobcache_destroy(cache);
    TEST_END();
}

static int test_jobcache_push_pop(void) {
    TEST_BEGIN();
    jobcache_t *cache = jobcache_create(4);
    ASSERT_NOT_NULL(cache);
    job_t job = {0};
    job.type = JOB_AGENT_CHAT;
    snprintf(job.instance_id, sizeof(job.instance_id), "default");
    snprintf(job.payload, sizeof(job.payload), "hello");
    job.payload_len = 5;
    int r = jobcache_push(cache, &job);
    ASSERT_EQ(r, 0);
    ASSERT_EQ(jobcache_depth(cache), (size_t)1);
    job_t out = {0};
    r = jobcache_pop(cache, &out, 0);
    ASSERT_EQ(r, 0);
    ASSERT_EQ(out.type, JOB_AGENT_CHAT);
    ASSERT_STR_EQ(out.instance_id, "default");
    ASSERT_TRUE(out.payload_len <= sizeof(out.payload));
    ASSERT_EQ(jobcache_depth(cache), (size_t)0);
    jobcache_destroy(cache);
    TEST_END();
}

static int test_jobcache_full(void) {
    TEST_BEGIN();
    jobcache_t *cache = jobcache_create(2);
    ASSERT_NOT_NULL(cache);
    job_t job = {0};
    job.type = JOB_PING;
    ASSERT_EQ(jobcache_push(cache, &job), 0);
    ASSERT_EQ(jobcache_push(cache, &job), 0);
    ASSERT_EQ(jobcache_push(cache, &job), -1);
    ASSERT_EQ(jobcache_depth(cache), (size_t)2);
    job_t out = {0};
    jobcache_pop(cache, &out, 0);
    jobcache_pop(cache, &out, 0);
    ASSERT_EQ(jobcache_pop(cache, &out, 0), -1);
    jobcache_destroy(cache);
    TEST_END();
}

static int test_jobcache_pop_timeout(void) {
    TEST_BEGIN();
    jobcache_t *cache = jobcache_create(2);
    ASSERT_NOT_NULL(cache);
    job_t out = {0};
    /* timeout 0 = return immediately when empty */
    int r = jobcache_pop(cache, &out, 0);
    ASSERT_EQ(r, -1);
    jobcache_destroy(cache);
    TEST_END();
}

static int test_jobcache_create_zero(void) {
    TEST_BEGIN();
    jobcache_t *cache = jobcache_create(0);
    /* implementation may return NULL or allow 0; we just don't crash */
    if (cache) {
        jobcache_destroy(cache);
    }
    TEST_END();
}

int test_jobcache_run(void) {
    int failed = 0;
    printf("  jobcache: create_destroy\n");
    if (test_jobcache_create_destroy() != 0) failed++;
    printf("  jobcache: push_pop\n");
    if (test_jobcache_push_pop() != 0) failed++;
    printf("  jobcache: full\n");
    if (test_jobcache_full() != 0) failed++;
    printf("  jobcache: pop_timeout\n");
    if (test_jobcache_pop_timeout() != 0) failed++;
    printf("  jobcache: create_zero\n");
    if (test_jobcache_create_zero() != 0) failed++;
    return failed;
}
