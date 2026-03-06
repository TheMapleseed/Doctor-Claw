#include "doctor.h"
#include "test_harness.h"
#include <stdio.h>
#include <string.h>

static int test_doctor_init_free(void) {
    TEST_BEGIN();
    doctor_report_t report = {0};
    int r = doctor_init(&report);
    ASSERT_EQ(r, 0);
    ASSERT_EQ(report.check_count, 0);
    doctor_free(&report);
    TEST_END();
}

static int test_doctor_run_checks(void) {
    TEST_BEGIN();
    doctor_report_t report = {0};
    doctor_init(&report);
    int r = doctor_run_checks(&report);
    ASSERT_EQ(r, 0);
    ASSERT_GE(report.check_count, 0);
    doctor_free(&report);
    TEST_END();
}

int test_doctor_run(void) {
    int failed = 0;
    printf("  doctor: init/free\n");
    if (test_doctor_init_free() != 0) failed++;
    printf("  doctor: run_checks\n");
    if (test_doctor_run_checks() != 0) failed++;
    return failed;
}
