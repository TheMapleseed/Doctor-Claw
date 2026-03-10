#include "test_harness.h"
#include "cron.h"
#include "security_monitor.h"
#include "runtime_monitor.h"
#include "runtime.h"
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
int test_security_monitor_run(void);
int test_muninn_run(void);
int test_log_run(void);
int test_observability_run(void);
int test_health_run(void);
int test_jobcache_run(void);
int test_jobworker_run(void);
int test_llama_run(void);
int test_agent_run(void);
int test_channels_run(void);
int test_cost_run(void);
int test_daemon_run(void);
int test_gateway_run(void);
int test_hardware_run(void);
int test_heartbeat_run(void);
int test_identity_run(void);
int test_ids_run(void);
int test_integrations_run(void);
int test_loop_guard_run(void);
int test_migration_run(void);
int test_onboard_run(void);
int test_pentest_run(void);
int test_peripherals_run(void);
int test_providers_run(void);
int test_rag_run(void);
int test_runtime_monitor_run(void);
int test_security_run(void);
int test_service_run(void);
int test_skillforge_run(void);
int test_skills_run(void);
int test_tunnel_run(void);
int test_instance_run(void);

int main(void) {
    /* Unbuffered stdout so output appears immediately and you see how far tests got */
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

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
    failed += test_security_monitor_run();
    fflush(stdout);
    failed += test_muninn_run();
    fflush(stdout);
    failed += test_log_run();
    fflush(stdout);
    failed += test_observability_run();
    fflush(stdout);
    failed += test_health_run();
    failed += test_jobcache_run();
    failed += test_jobworker_run();
    printf("  [suite] llama\n");
    fflush(stdout);
    failed += test_llama_run();
    failed += test_agent_run();
    failed += test_channels_run();
    failed += test_cost_run();
    failed += test_daemon_run();
    failed += test_gateway_run();
    failed += test_hardware_run();
    failed += test_heartbeat_run();
    failed += test_identity_run();
    failed += test_ids_run();
    failed += test_integrations_run();
    failed += test_loop_guard_run();
    failed += test_migration_run();
    failed += test_onboard_run();
    failed += test_pentest_run();
    failed += test_peripherals_run();
    failed += test_providers_run();
    failed += test_rag_run();
    failed += test_runtime_monitor_run();
    failed += test_security_run();
    failed += test_service_run();
    failed += test_skillforge_run();
    failed += test_skills_run();
    failed += test_tunnel_run();
    failed += test_instance_run();

    printf("====================\n");
    if (failed > 0) {
        printf("FAILED: %d test(s)\n", failed);
        printf("(runtime tests concluded)\n");
        fflush(stdout);
        /* Graceful teardown so globals (cron, security_monitor, runtime, etc.) are shut down before exit */
        cron_shutdown();
        security_monitor_shutdown();
        runtime_monitor_shutdown();
        runtime_shutdown();
        return 1;
    }
    printf("All tests passed.\n");
    printf("(runtime tests concluded)\n");
    fflush(stdout);
    /* Graceful teardown when tests complete (build-and-test flow) */
    cron_shutdown();
    security_monitor_shutdown();
    runtime_monitor_shutdown();
    runtime_shutdown();
    return 0;
}
