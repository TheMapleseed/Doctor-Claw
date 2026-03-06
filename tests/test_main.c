#include "test_harness.h"
#include <stdio.h>
#include <stdlib.h>

int g_test_failed;
int g_test_run;

int test_config_run(void);
int test_memory_run(void);
int test_tools_run(void);
int test_cron_run(void);
int test_approval_run(void);
int test_auth_run(void);
int test_doctor_run(void);
int test_runtime_run(void);
int test_util_run(void);

int main(void) {
    int failed = 0;
    printf("DoctorClaw test suite\n");
    printf("====================\n");

    failed += test_config_run();
    failed += test_memory_run();
    failed += test_tools_run();
    failed += test_cron_run();
    failed += test_approval_run();
    failed += test_auth_run();
    failed += test_doctor_run();
    failed += test_runtime_run();
    failed += test_util_run();

    printf("====================\n");
    if (failed > 0) {
        printf("FAILED: %d test(s)\n", failed);
        return 1;
    }
    printf("All tests passed.\n");
    return 0;
}
