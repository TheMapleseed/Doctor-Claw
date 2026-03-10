#define DoctorClaw_VERSION "0.1.0"
#define DoctorClaw_NAME "Doctor Claw"
#define DoctorClaw_TAGLINE "Zero overhead. Zero compromise. 100% C."

#include "c23_check.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>

#include "config.h"
#include "providers.h"
#include "channels.h"
#include "memory.h"
#include "tools.h"
#include "agent.h"
#include "auth.h"
#include "service.h"
#include "migration.h"
#include "skills.h"
#include "onboard.h"
#include "doctor.h"
#include "health.h"
#include "heartbeat.h"
#include "identity.h"
#include "integrations.h"
#include "observability.h"
#include "cost.h"
#include "hardware.h"
#include "rag.h"
#include "runtime.h"
#include "security.h"
#include "tunnel.h"
#include "util.h"
#include "gateway.h"
#include "jobcache.h"
#include "jobworker.h"
#include "instance.h"
#include "observability.h"
#include "log.h"
#include "migration.h"
#include "cron.h"
#include "pentest.h"

typedef enum {
    CMD_ONBOARD,
    CMD_AGENT,
    CMD_GATEWAY,
    CMD_DAEMON,
    CMD_SERVICE,
    CMD_DOCTOR,
    CMD_STATUS,
    CMD_CRON,
    CMD_MODELS,
    CMD_PROVIDERS,
    CMD_CHANNEL,
    CMD_INTEGRATIONS,
    CMD_SKILLS,
    CMD_MIGRATE,
    CMD_AUTH,
    CMD_HARDWARE,
    CMD_PERIPHERAL,
    CMD_VERIFY_TASK_FOCUS,
    CMD_LOG,
    CMD_PENTEST,
    CMD_STOP,
    CMD_TEST,
    CMD_UNKNOWN
} command_t;

static bool g_verbose = false;
static volatile sig_atomic_t g_daemon_shutdown = 0;

static void daemon_signal_handler(int sig) {
    (void)sig;
    g_daemon_shutdown = 1;
}

static void print_version(void) {
    printf("%s v%s\n", DoctorClaw_NAME, DoctorClaw_VERSION);
    printf("%s\n", DoctorClaw_TAGLINE);
}

static void print_usage(const char *prog) {
    printf("Usage: %s <command> [options]\n\n", prog);
    printf("Commands:\n");
    printf("  onboard        Initialize workspace and configuration\n");
    printf("  agent           Start the AI agent loop\n");
    printf("  gateway        Start the gateway server (webhooks, websockets)\n");
    printf("  daemon         Start long-running autonomous runtime\n");
    printf("  service        Manage OS service lifecycle\n");
    printf("  doctor         Run diagnostics\n");
    printf("  status         Show system status\n");
    printf("  cron           Manage scheduled tasks\n");
    printf("  models         Manage provider model catalogs\n");
    printf("  providers      List supported AI providers\n");
    printf("  channel        Manage channels (telegram, discord, slack)\n");
    printf("  integrations   Browse integrations\n");
    printf("  skills         Manage skills (user-defined capabilities)\n");
    printf("  migrate        Migrate data from other agent runtimes\n");
    printf("  auth           Manage provider authentication profiles\n");
    printf("  hardware      Discover USB hardware\n");
    printf("  peripheral     Manage hardware peripherals\n");
    printf("  verify-task-focus  Verify task-focus (attention loop) functionality\n");
    printf("  log            Logging utilities (export)\n");
    printf("  pentest        Run security penetration tests against gateway\n");
    printf("  stop           Gracefully stop the running daemon (sends SIGTERM)\n");
    printf("  test           Run runtime tests (then exit; use from CLI or model)\n");
    printf("\nOptions:\n");
    printf("  -h, --help     Show this help message\n");
    printf("  -v, --version  Show version information\n");
    printf("  --verbose      Enable verbose logging\n");
}

static command_t parse_command(const char *cmd) {
    if (strcmp(cmd, "onboard") == 0) return CMD_ONBOARD;
    if (strcmp(cmd, "agent") == 0) return CMD_AGENT;
    if (strcmp(cmd, "gateway") == 0) return CMD_GATEWAY;
    if (strcmp(cmd, "daemon") == 0) return CMD_DAEMON;
    if (strcmp(cmd, "service") == 0) return CMD_SERVICE;
    if (strcmp(cmd, "doctor") == 0) return CMD_DOCTOR;
    if (strcmp(cmd, "status") == 0) return CMD_STATUS;
    if (strcmp(cmd, "cron") == 0) return CMD_CRON;
    if (strcmp(cmd, "models") == 0) return CMD_MODELS;
    if (strcmp(cmd, "providers") == 0) return CMD_PROVIDERS;
    if (strcmp(cmd, "channel") == 0) return CMD_CHANNEL;
    if (strcmp(cmd, "integrations") == 0) return CMD_INTEGRATIONS;
    if (strcmp(cmd, "skills") == 0) return CMD_SKILLS;
    if (strcmp(cmd, "migrate") == 0) return CMD_MIGRATE;
    if (strcmp(cmd, "auth") == 0) return CMD_AUTH;
    if (strcmp(cmd, "hardware") == 0) return CMD_HARDWARE;
    if (strcmp(cmd, "peripheral") == 0) return CMD_PERIPHERAL;
    if (strcmp(cmd, "verify-task-focus") == 0) return CMD_VERIFY_TASK_FOCUS;
    if (strcmp(cmd, "log") == 0) return CMD_LOG;
    if (strcmp(cmd, "pentest") == 0) return CMD_PENTEST;
    if (strcmp(cmd, "stop") == 0) return CMD_STOP;
    if (strcmp(cmd, "test") == 0) return CMD_TEST;
    return CMD_UNKNOWN;
}

static void ensure_log_dirs(const config_t *cfg) {
    if (!cfg) return;
    if (cfg->paths.workspace_dir[0]) mkdir(cfg->paths.workspace_dir, 0755);
    if (cfg->paths.state_dir[0]) mkdir(cfg->paths.state_dir, 0755);
    if (cfg->paths.data_dir[0]) mkdir(cfg->paths.data_dir, 0755);
}

static void configure_logging_default(const config_t *cfg) {
    if (!cfg) return;
    log_init();

    const char *env_path = getenv("DOCTORCLAW_LOG_FILE");
    if (env_path && env_path[0]) {
        (void)log_set_file(env_path);
        return;
    }

    if (cfg->paths.state_dir[0]) {
        char path[MAX_PATH_LEN];
        snprintf(path, sizeof(path), "%s/doctorclaw.log", cfg->paths.state_dir);
        (void)log_set_file(path);
    }
}

static int cmd_onboard(int argc, char **argv) {
    onboard_t ob;
    onboard_init(&ob);
    
    char *workspace = (argc > 0) ? argv[0] : ".doctorclaw";
    
    if (g_verbose) printf("[DoctorClaw] Onboarding workspace: %s\n", workspace);
    
    int result = onboard_execute(&ob, workspace);
    
    if (result == 0) {
        printf("Onboarding completed successfully!\n");
    } else {
        printf("Onboarding failed: %s\n", ob.error);
    }
    
    onboard_free(&ob);
    return result;
}

static int cmd_verify_task_focus(int argc, char **argv) {
    (void)argc;
    (void)argv;
    printf("[DoctorClaw] Verifying task-focus (attention loop) functionality...\n\n");
    int failed = 0;

    const char *marker = agent_task_complete_marker();
    if (!marker || strcmp(marker, "[TASK_COMPLETE]") != 0) {
        printf("  FAIL: agent_task_complete_marker() should return \"[TASK_COMPLETE]\"\n");
        failed++;
    } else {
        printf("  OK: agent_task_complete_marker() == \"[TASK_COMPLETE]\"\n");
    }

    char buf[256];
    if (agent_run_task(NULL, "task", buf, sizeof(buf)) != -1) {
        printf("  FAIL: agent_run_task(NULL, ...) should return -1\n");
        failed++;
    } else {
        printf("  OK: agent_run_task(NULL, ...) returns -1\n");
    }

    config_t cfg;
    config_init_defaults(&cfg);
    config_load(NULL, &cfg);
    agent_t agent;
    if (agent_init(&agent, &cfg) != 0) {
        printf("  FAIL: agent_init failed\n");
        return -1;
    }
    if (agent_run_task(&agent, NULL, buf, sizeof(buf)) != -1) {
        printf("  FAIL: agent_run_task(agent, NULL, ...) should return -1\n");
        failed++;
    } else {
        printf("  OK: agent_run_task(agent, NULL, ...) returns -1\n");
    }
    char response[4096];
    int r = agent_run_task(&agent, "Say only [TASK_COMPLETE] and nothing else.", response, sizeof(response));
    agent_free(&agent);
    if (r == 0) {
        printf("  OK: agent_run_task ran (with API would loop until [TASK_COMPLETE])\n");
    } else {
        printf("  OK: agent_run_task returned %d (expected without API key: error)\n", r);
    }

    printf("\n");
    if (failed) {
        printf("Verification FAILED (%d check(s) failed).\n", failed);
        return -1;
    }
    printf("Task-focus verification passed.\n");
    return 0;
}

static int cmd_agent(int argc, char **argv) {
    printf("[DoctorClaw] Starting AI agent...\n\n");
    
    config_t cfg;
    config_init_defaults(&cfg);
    config_load(NULL, &cfg);
    
    agent_t agent;
    agent_init(&agent, &cfg);
    
    if (argc > 0) {
        printf("Running agent with prompt: %s\n", argv[0]);
        int result = agent_start(&agent, argv[0]);
        
        if (result == 0) {
            printf("\nAgent started. Running loop...\n");
            agent_run_loop(&agent);
        } else {
            printf("Agent error: failed to start\n");
        }
    } else {
        printf("Interactive mode - type 'exit' to quit\n");
        char input[4096] = {0};
        
        while (1) {
            printf("\n> ");
            if (fgets(input, sizeof(input), stdin) == NULL) break;
            input[strcspn(input, "\n")] = 0;
            
            if (strcmp(input, "exit") == 0) break;
            if (strlen(input) == 0) continue;
            
            int result = agent_start(&agent, input);
            if (result == 0) {
                agent_run_loop(&agent);
            } else {
                printf("Error: failed to start agent\n");
            }
        }
    }
    
    agent_free(&agent);
    
    return 0;
}

static int cmd_gateway(int argc, char **argv) {
    printf("[DoctorClaw] Gateway server\n");
    printf("=======================\n\n");
    
    int port = 8080;
    const char *host = "0.0.0.0";
    
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-h") == 0 && i + 1 < argc) {
            host = argv[++i];
        } else if (argv[i][0] != '-') {
            port = atoi(argv[i]);
        }
    }
    
    config_t cfg;
    config_init_defaults(&cfg);
    config_load(NULL, &cfg);
    ensure_log_dirs(&cfg);
    configure_logging_default(&cfg);
    observability_global_init();

    printf("Starting HTTP server on http://%s:%d...\n", host, port);
    printf("Press Ctrl+C to stop\n\n");
    
    printf("Gateway endpoints:\n");
    printf("  GET  /              - Index page\n");
    printf("  GET  /health        - Health check\n");
    printf("  POST /webhook       - Webhook receiver\n");
    printf("  POST /telegram      - Telegram bot updates\n");
    printf("  POST /discord       - Discord webhooks\n");
    printf("  POST /slack         - Slack events\n");
    printf("  POST /agent/chat    - Agent chat API\n");
    
    return gateway_run(host, (uint16_t)port, &cfg, NULL);
}

typedef struct {
    const char *host;
    uint16_t port;
    config_t *config;
    jobcache_t *jobcache;
} gateway_thread_args_t;

static void *gateway_thread_fn(void *arg) {
    gateway_thread_args_t *a = (gateway_thread_args_t *)arg;
    (void)gateway_run(a->host, a->port, a->config, a->jobcache);
    return NULL;
}

static int cmd_daemon(int argc, char **argv) {
    (void)argc;
    (void)argv;
    printf("[DoctorClaw] Daemon mode (shared job cache, load-based workers)\n");
    printf("===================\n\n");
    
    config_t cfg;
    config_load(NULL, &cfg);
    ensure_log_dirs(&cfg);
    configure_logging_default(&cfg);
    observability_global_init();
    uint16_t port = cfg.gateway.port;
    if (port == 0) port = 8080;
    
    runtime_init();
    runtime_start();
    
    runtime_info_t info;
    runtime_get_info(&info);
    
    jobcache_t *cache = jobcache_create(256);
    if (!cache) {
        printf("[Daemon] Failed to create job cache\n");
        runtime_stop();
        runtime_shutdown();
        return -1;
    }
    instance_init();
    instance_register("default", &cfg);
    jobworker_pool_t *pool = jobworker_pool_create(cache, instance_get_config, &cfg, 2, 16);
    if (!pool) {
        jobcache_destroy(cache);
        runtime_stop();
        runtime_shutdown();
        return -1;
    }
    if (jobworker_pool_start(pool) != 0) {
        jobworker_pool_destroy(pool);
        jobcache_destroy(cache);
        runtime_stop();
        runtime_shutdown();
        return -1;
    }
    cron_set_jobcache(cache);
    
    log_info("Daemon started PID=%d gateway=http://%s:%u workers=2-16", getpid(), cfg.gateway.host[0] ? cfg.gateway.host : "0.0.0.0", (unsigned)port);
    printf("Daemon started with PID: %d\n", getpid());
    printf("State: Running\n");
    printf("Uptime: %lu seconds\n", (unsigned long)info.uptime_seconds);
    printf("Gateway: http://%s:%u (agent, health, webhooks)\n", cfg.gateway.host[0] ? cfg.gateway.host : "0.0.0.0", (unsigned)port);
    printf("Job cache: shared; workers: 2–16 (load-based)\n");
    
    pthread_t gateway_thread;
    gateway_thread_args_t gw_args = {
        .host = cfg.gateway.host[0] ? cfg.gateway.host : "0.0.0.0",
        .port = port,
        .config = &cfg,
        .jobcache = cache
    };
    if (pthread_create(&gateway_thread, NULL, gateway_thread_fn, &gw_args) != 0) {
        printf("[Daemon] Warning: could not start gateway thread; HTTP will not be available.\n");
    }

    if (cfg.paths.workspace_dir[0]) {
        char channels_path[MAX_PATH_LEN];
        snprintf(channels_path, sizeof(channels_path), "%s/channels/config.toml", cfg.paths.workspace_dir);
        if (channel_start_listeners(channels_path, cache) == 0) {
            printf("[Daemon] Channel listeners started (Telegram poll)\n");
        }
    }

    printf("\nDaemon running... (Press Ctrl+C or run 'doctorclaw stop' to stop)\n");
    cron_set_workspace(cfg.paths.state_dir);
    cron_init();

    signal(SIGINT, daemon_signal_handler);
    signal(SIGTERM, daemon_signal_handler);

    char pid_path[512];
    snprintf(pid_path, sizeof(pid_path), "%s/doctorclaw.pid", cfg.paths.state_dir[0] ? cfg.paths.state_dir : ".");
    FILE *pf = fopen(pid_path, "w");
    if (pf) {
        fprintf(pf, "%d\n", getpid());
        fclose(pf);
    }

    /* Optional: run runtime tests once at startup. Set DOCTORCLAW_RUN_STARTUP_TESTS=1. Tests run in a child, complete and shut down; main loop keeps running. */
    if (getenv("DOCTORCLAW_RUN_STARTUP_TESTS") != NULL && getenv("DOCTORCLAW_RUN_STARTUP_TESTS")[0] != '0') {
        const char *test_bin = getenv("DOCTORCLAW_TEST_BIN");
        char path_buf[1024];
        if (!test_bin || !test_bin[0]) {
            snprintf(path_buf, sizeof(path_buf), "bin/doctorclaw_test");
            if (access(path_buf, X_OK) != 0) snprintf(path_buf, sizeof(path_buf), "doctorclaw_test");
            test_bin = path_buf;
        } else {
            snprintf(path_buf, sizeof(path_buf), "%s", test_bin);
            test_bin = path_buf;
        }
        pid_t tpid = fork();
        if (tpid == 0) {
            execl(test_bin, "doctorclaw_test", (char *)NULL);
            execlp("doctorclaw_test", "doctorclaw_test", (char *)NULL);
            _exit(127);
        }
        if (tpid > 0) {
            int tstatus = 0;
            while (waitpid(tpid, &tstatus, 0) < 0 && errno == EINTR) { /* spin */ }
            printf("[Daemon] Startup tests completed (exit %d)\n", WIFEXITED(tstatus) ? WEXITSTATUS(tstatus) : -1);
        }
    }

    while (!g_daemon_shutdown) {
        cron_run_pending();
        for (int i = 0; i < 10 && !g_daemon_shutdown; i++) sleep(1);
        runtime_get_info(&info);
        if (g_verbose) printf(".");
    }

    log_info("Daemon shutting down gracefully");
    printf("\nShutting down gracefully...\n");
    gateway_request_shutdown();
    pthread_join(gateway_thread, NULL);
    (void)unlink(pid_path);

    cron_set_jobcache(NULL);
    jobworker_pool_stop(pool);
    jobworker_pool_destroy(pool);
    jobcache_destroy(cache);
    instance_shutdown();
    cron_shutdown();
    runtime_stop();
    runtime_shutdown();

    printf("Daemon stopped.\n");
    return 0;
}

static int cmd_stop(int argc, char **argv) {
    (void)argc;
    (void)argv;
    config_t cfg;
    config_load(NULL, &cfg);
    char pid_path[512];
    snprintf(pid_path, sizeof(pid_path), "%s/doctorclaw.pid",
             cfg.paths.state_dir[0] ? cfg.paths.state_dir : ".");
    FILE *pf = fopen(pid_path, "r");
    if (!pf) {
        fprintf(stderr, "No daemon PID file at %s (daemon not running?)\n", pid_path);
        return 1;
    }
    int pid = 0;
    if (fscanf(pf, "%d", &pid) != 1 || pid <= 0) {
        fclose(pf);
        fprintf(stderr, "Invalid PID file\n");
        return 1;
    }
    fclose(pf);
    if (kill(pid, SIGTERM) != 0) {
        fprintf(stderr, "Failed to send SIGTERM to PID %d: %s\n", pid, strerror(errno));
        return 1;
    }
    printf("Sent SIGTERM to daemon (PID %d). It will shut down gracefully.\n", pid);
    return 0;
}

static int cmd_test(int argc, char **argv) {
    (void)argc;
    (void)argv;
    char path_buf[1024];
    const char *test_bin = getenv("DOCTORCLAW_TEST_BIN");
    if (test_bin && test_bin[0]) {
        snprintf(path_buf, sizeof(path_buf), "%s", test_bin);
        test_bin = path_buf;
    } else {
        const char *try_paths[] = { "bin/doctorclaw_test", "./bin/doctorclaw_test", NULL };
        test_bin = NULL;
        for (size_t i = 0; try_paths[i] != NULL; i++) {
            snprintf(path_buf, sizeof(path_buf), "%s", try_paths[i]);
            if (access(path_buf, X_OK) == 0) {
                test_bin = path_buf;
                break;
            }
        }
        if (!test_bin) test_bin = "doctorclaw_test"; /* last resort: from PATH */
    }
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }
    if (pid == 0) {
        if (test_bin != path_buf) execlp(test_bin, "doctorclaw_test", (char *)NULL);
        execl(test_bin, "doctorclaw_test", (char *)NULL);
        _exit(127);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) { /* spin */ }
    if (!WIFEXITED(status)) return 1;
    return WEXITSTATUS(status);
}

static int cmd_service(int argc, char **argv) {
    printf("[DoctorClaw] Service management\n");
    printf("===========================\n\n");
    
    service_manager_t mgr;
    service_manager_init(&mgr);
    
    if (argc == 0) {
        printf("Usage: doctorclaw service <command> [args]\n");
        printf("Commands:\n");
        printf("  list                List all services\n");
        printf("  register <name> <path>   Register a service\n");
        printf("  start <name>        Start a service\n");
        printf("  stop <name>         Stop a service\n");
        printf("  enable <name>      Enable auto-start\n");
        printf("  disable <name>     Disable auto-start\n");
        return 0;
    }
    
    if (strcmp(argv[0], "list") == 0) {
        service_t *services;
        size_t count;
        service_list(&mgr, &services, &count);
        
        printf("Services (%zu registered):\n", count);
        for (size_t i = 0; i < count; i++) {
            printf("  %-20s %s (pid: %d)\n", 
                   services[i].name, 
                   service_state_name(services[i].state),
                   services[i].pid);
        }
    } else if (strcmp(argv[0], "register") == 0 && argc >= 3) {
        service_register(&mgr, argv[1], argv[2], NULL);
        printf("Service '%s' registered at %s\n", argv[1], argv[2]);
    } else if (strcmp(argv[0], "start") == 0 && argc >= 2) {
        int result = service_start(&mgr, argv[1]);
        printf("Service '%s' %s\n", argv[1], result == 0 ? "started" : "not found");
    } else if (strcmp(argv[0], "stop") == 0 && argc >= 2) {
        int result = service_stop(&mgr, argv[1]);
        printf("Service '%s' %s\n", argv[1], result == 0 ? "stopped" : "not found");
    } else if (strcmp(argv[0], "enable") == 0 && argc >= 2) {
        service_enable(&mgr, argv[1]);
        printf("Service '%s' enabled\n", argv[1]);
    } else if (strcmp(argv[0], "disable") == 0 && argc >= 2) {
        service_disable(&mgr, argv[1]);
        printf("Service '%s' disabled\n", argv[1]);
    }
    
    service_manager_free(&mgr);
    return 0;
}

static int cmd_doctor(int argc, char **argv) {
    (void)argc;
    (void)argv;
    printf("[DoctorClaw] Running diagnostics...\n\n");
    
    doctor_report_t report;
    doctor_init(&report);
    doctor_run_checks(&report);
    doctor_print_report(&report);
    doctor_free(&report);
    
    return 0;
}

static int cmd_status(int argc, char **argv) {
    (void)argc;
    (void)argv;
    printf("Doctor Claw Status\n");
    printf("==================\n\n");
    printf("Version:     %s\n", DoctorClaw_VERSION);
    printf("Platform:    C23/Clang\n");
    printf("Build:       %s %s\n", __DATE__, __TIME__);
    printf("\n");
    
    runtime_info_t rt;
    runtime_get_info(&rt);
    printf("Runtime:\n");
    printf("  State:    %s\n", rt.state == RUNTIME_STATE_RUNNING ? "Running" : "Idle");
    printf("  Uptime:   %lu seconds\n", (unsigned long)rt.uptime_seconds);
    printf("\n");
    
    health_monitor_t hm;
    health_init(&hm);
    health_register(&hm, "providers");
    health_register(&hm, "memory");
    health_register(&hm, "channels");
    
    health_check_t *checks;
    size_t count;
    health_list(&hm, &checks, &count);
    
    printf("Components:\n");
    for (size_t i = 0; i < count; i++) {
        printf("  %-15s %s\n", checks[i].component, 
               checks[i].status == HEALTH_OK ? "OK" : "Unknown");
    }
    
    health_free(&hm);
    printf("\nStatus: Ready\n");
    
    return 0;
}

static int cmd_cron(int argc, char **argv) {
    printf("[DoctorClaw] Cron scheduler\n");
    printf("=======================\n\n");
    
    if (argc == 0) {
        printf("Usage: doctorclaw cron <command> [args]\n");
        printf("Commands:\n");
        printf("  list                    List scheduled tasks\n");
        printf("  add <id> <expr> <cmd>   Add a cron task (e.g. add job1 \"* * * * *\" \"echo hi\")\n");
        printf("  remove <id>             Remove a task by id\n");
        return 0;
    }
    
    if (strcmp(argv[0], "list") == 0) {
        if (cron_init() == 0) {
            cron_list_tasks();
            cron_shutdown();
        }
    } else if (strcmp(argv[0], "add") == 0 && argc >= 4) {
        const char *id = argv[1];
        const char *expr = argv[2];
        const char *cmd = argv[3];
        if (cron_init() == 0) {
            int r = cron_add_task(id, expr, cmd);
            cron_shutdown();
            if (r != 0) {
                printf("Failed to add task (duplicate id or max tasks)\n");
                return -1;
            }
        } else {
            return -1;
        }
    } else if (strcmp(argv[0], "remove") == 0 && argc >= 2) {
        const char *id = argv[1];
        if (cron_init() == 0) {
            int r = cron_remove_task(id);
            cron_shutdown();
            if (r != 0) {
                printf("Task '%s' not found\n", id);
                return -1;
            }
        } else {
            return -1;
        }
    } else if ((strcmp(argv[0], "add") == 0 && argc < 4) || (strcmp(argv[0], "remove") == 0 && argc < 2)) {
        printf("Usage: cron add <id> <expression> <command>\n");
        printf("       cron remove <id>\n");
        return -1;
    }
    
    return 0;
}

static int cmd_models(int argc, char **argv) {
    (void)argc;
    (void)argv;
    printf("Available Models\n");
    printf("================\n\n");
    
    printf("OpenAI:\n");
    printf("  gpt-4o           Latest GPT-4\n");
    printf("  gpt-4o-mini      FastGPT-4\n");
    printf("  gpt-4-turbo      GPT-4 Turbo\n");
    printf("  gpt-3.5-turbo    GPT-3.5\n\n");
    
    printf("Anthropic:\n");
    printf("  claude-3-5-sonnet Latest Claude\n");
    printf("  claude-3-opus    Claude 3 Opus\n");
    printf("  claude-3-haiku   Claude 3 Haiku\n\n");
    
    printf("OpenRouter:\n");
    printf("  openai/gpt-4o-mini\n");
    printf("  anthropic/claude-3-haiku\n");
    printf("  google/gemini-pro\n\n");
    
    printf("Local:\n");
    printf("  llama-7b         Llama 7B (llama.cpp)\n");
    printf("  mistral-7b       Mistral 7B\n");
    
    return 0;
}

static int cmd_providers(int argc, char **argv) {
    (void)argc;
    (void)argv;
    printf("Supported AI Providers\n");
    printf("=====================\n\n");
    
    printf("  openrouter       OpenRouter aggregator (default)\n");
    printf("  anthropic        Anthropic (Claude)\n");
    printf("  openai           OpenAI\n");
    printf("  openai-codex    OpenAI Codex\n");
    printf("  llama            llama.cpp (local GGUF models)\n");
    printf("  ollama           Ollama (local)\n");
    printf("  gemini           Google Gemini\n");
    printf("  glm              Zhipu GLM\n");
    printf("  copilot          GitHub Copilot\n");
    printf("  compatible       OpenAI-compatible API\n");
    
    printf("\nEnvironment Variables (all keys detected at startup; config overrides from env):\n");
    static const struct { const char *name; const char *desc; } env_descriptions[] = {
        { "DOCTORCLAW_WORKSPACE", "Override workspace/state/data dirs" },
        { "DOCTORCLAW_CONFIG", "Config file path" },
        { "DOCTORCLAW_PROVIDER", "Default provider (openrouter, openai, anthropic, gemini, llama, ollama, ...)" },
        { "DOCTORCLAW_MODEL", "Default model name" },
        { "DOCTORCLAW_GATEWAY_PORT", "Gateway port" },
        { "DOCTORCLAW_GATEWAY_HOST", "Gateway host bind" },
        { "DOCTORCLAW_LLAMA_MODEL", "Path to GGUF model (when provider=llama)" },
        { "DOCTORCLAW_LOG_FILE", "Log file path" },
        { "DOCTORCLAW_RUN_STARTUP_TESTS", "Set to 1 to run tests once at daemon start" },
        { "DOCTORCLAW_TEST_BIN", "Path to doctorclaw_test binary" },
        { "DOCTORCLAW_HEALTH_SECRET", "Optional secret for GET /health" },
        { "DOCTORCLAW_SHELL", "Shell for tool execution (default /bin/sh)" },
        { "OPENROUTER_API_KEY", "OpenRouter API key" },
        { "OPENAI_API_KEY", "OpenAI API key" },
        { "ANTHROPIC_API_KEY", "Anthropic API key" },
        { "GEMINI_API_KEY", "Google Gemini API key" },
        { "GITHUB_TOKEN", "GitHub/Copilot and integrations" },
        { "OLLAMA_HOST", "Ollama host (default localhost:11434)" },
        { "JIRA_API_TOKEN", "Jira integration (Bearer)" },
        { "NOTION_API_KEY", "Notion integration" },
        { "HOME", "User home (paths)" },
        { "USER", "Username" },
        { "HTTP_PROXY", "HTTP proxy" },
        { "HTTPS_PROXY", "HTTPS proxy" },
        { "NO_PROXY", "No-proxy list" },
        { NULL, NULL }
    };
    for (int i = 0; i < config_env_var_count(); i++) {
        const char *name = config_env_var_name(i);
        if (!name) continue;
        const char *desc = "";
        for (int j = 0; env_descriptions[j].name; j++)
            if (strcmp(env_descriptions[j].name, name) == 0) {
                desc = env_descriptions[j].desc;
                break;
            }
        const char *set = getenv(name) && getenv(name)[0] ? " (set)" : "";
        printf("  %-28s - %s%s\n", name, desc, set);
    }
    return 0;
}

static int cmd_channel(int argc, char **argv) {
    printf("[DoctorClaw] Channel management\n");
    printf("===========================\n\n");
    
    char config_path[1024];
    const char *home = getenv("HOME");
    snprintf(config_path, sizeof(config_path), "%s/.doctorclaw/channels.conf", home && home[0] ? home : ".");

    if (argc == 0) {
        printf("Usage: doctorclaw channel <command> [args]\n");
        printf("Commands:\n");
        printf("  list              List configured channels\n");
        printf("  add <type>        Add a channel (telegram, discord, slack)\n");
        printf("  remove <name>     Remove a channel\n");
        printf("  start [name]      Start channel listener(s)\n");
        printf("  stop <name>       Stop a channel listener\n");
        return 0;
    }

    if (strcmp(argv[0], "list") == 0) {
        channels_load_config(config_path);
        channel_config_t *configs = NULL;
        size_t count = 0;
        if (channel_list_configured(&configs, &count) == 0 && configs) {
            printf("Configured channels (%zu):\n", count);
            for (size_t i = 0; i < count; i++) {
                printf("  %-12s %s (enabled=%d)\n",
                       configs[i].name[0] ? configs[i].name : channel_get_name(configs[i].type),
                       channel_get_name(configs[i].type), configs[i].enabled);
            }
        } else {
            printf("No channels configured. Use 'doctorclaw channel add telegram' (or discord, slack).\n");
        }
        return 0;
    }

    if (strcmp(argv[0], "add") == 0 && argc >= 2) {
        channel_type_t type = channel_get_type(argv[1]);
        if (type == CHANNEL_UNKNOWN) {
            printf("Unknown channel type. Use: telegram, discord, slack.\n");
            return -1;
        }
        channels_load_config(config_path);
        channel_config_t cfg = {0};
        snprintf(cfg.name, sizeof(cfg.name), "%s", argv[1]);
        cfg.type = type;
        cfg.enabled = false;
        channel_t dummy;
        if (channel_init(&dummy, type, &cfg) == 0 && channels_save_config(config_path) == 0) {
            printf("Added channel '%s'. Edit %s to set bot_token or webhook_url, then 'channel start'.\n", argv[1], config_path);
        } else {
            printf("Failed to add channel or save config.\n");
            return -1;
        }
        return 0;
    }

    if (strcmp(argv[0], "start") == 0) {
        channels_load_config(config_path);
        if (channel_start_all() == 0) {
            printf("Channel listeners started.\n");
        } else {
            printf("No channels to start or start failed.\n");
        }
        return 0;
    }

    return 0;
}

static int cmd_integrations(int argc, char **argv) {
    (void)argc;
    (void)argv;
    printf("[DoctorClaw] Integrations\n");
    printf("======================\n\n");
    
    integrations_manager_t mgr;
    integrations_init(&mgr);
    
    integrations_add(&mgr, "github", NULL);
    integrations_add(&mgr, "jira", NULL);
    integrations_add(&mgr, "notion", NULL);
    integrations_add(&mgr, "slack", NULL);
    
    integration_t *list;
    size_t count;
    integrations_list(&mgr, &list, &count);
    
    printf("Available integrations:\n");
    for (size_t i = 0; i < count; i++) {
        printf("  %-15s %s\n", list[i].name, list[i].enabled ? "enabled" : "disabled");
    }
    
    integrations_free(&mgr);
    return 0;
}

static int cmd_skills(int argc, char **argv) {
    printf("[DoctorClaw] Skills management\n");
    printf("===========================\n\n");
    
    skills_manager_t mgr;
    skills_manager_init(&mgr);
    
    if (argc == 0) {
        printf("Usage: doctorclaw skills <command> [args]\n");
        printf("Commands:\n");
        printf("  list              List available skills\n");
        printf("  add <name> <path> Add a skill\n");
        printf("  run <name>        Run a skill\n");
        printf("  enable <name>     Enable a skill\n");
        printf("  disable <name>    Disable a skill\n");
        
        skill_t *list;
        size_t count;
        skills_list(&mgr, &list, &count);
        
        printf("\nInstalled skills (%zu):\n", count);
        for (size_t i = 0; i < count; i++) {
            printf("  %-20s %s\n", list[i].name, 
                   list[i].enabled ? "enabled" : "disabled");
        }
        
        skills_manager_free(&mgr);
        return 0;
    }
    
    if (strcmp(argv[0], "list") == 0) {
        skill_t *list;
        size_t count;
        skills_list(&mgr, &list, &count);
        
        printf("Skills (%zu):\n", count);
        for (size_t i = 0; i < count; i++) {
            printf("  %-20s %s\n", list[i].name, 
                   list[i].enabled ? "enabled" : "disabled");
        }
    } else if (strcmp(argv[0], "add") == 0 && argc >= 3) {
        skills_add(&mgr, argv[1], "User-defined skill", SKILL_TYPE_TOOL, argv[2]);
        printf("Skill '%s' added\n", argv[1]);
    } else if (strcmp(argv[0], "run") == 0 && argc >= 2) {
        char output[1024] = {0};
        skills_execute(&mgr, argv[1], NULL, output, sizeof(output));
        printf("%s\n", output);
    } else if (strcmp(argv[0], "enable") == 0 && argc >= 2) {
        skills_enable(&mgr, argv[1]);
    } else if (strcmp(argv[0], "disable") == 0 && argc >= 2) {
        skills_disable(&mgr, argv[1]);
    }
    
    skills_manager_free(&mgr);
    return 0;
}

static int cmd_migrate(int argc, char **argv) {
    printf("[DoctorClaw] Migration\n");
    printf("====================\n\n");
    
    if (argc == 0) {
        printf("Usage: doctorclaw migrate <command> [args]\n");
        printf("Commands:\n");
        printf("  list              List migration sources\n");
        printf("  run <source>      Run migration from source\n");
        printf("  sources           Show available sources\n");
        return 0;
    }
    
    if (strcmp(argv[0], "list") == 0 || strcmp(argv[0], "sources") == 0) {
        printf("Available migration sources:\n");
        printf("  claude      - Claude Code\n");
        printf("  openai      - OpenAI\n");
        printf("  ollama      - Ollama\n");
        printf("  llamacpp    - llama.cpp\n");
        printf("  generic     - Generic JSON export (usage: migrate run generic <path-to.json>)\n");
    } else if (strcmp(argv[0], "run") == 0 && argc >= 3) {
        const char *source_name = argv[1];
        const char *source_path = argv[2];
        migration_source_t src = migration_source_from_name(source_name);
        if (src == MIGRATION_SOURCE_NONE) {
            printf("Unknown source '%s'. Use: generic, claude, openai, ollama, llamacpp.\n", source_name);
            return -1;
        }
        if (src != MIGRATION_SOURCE_GENERIC) {
            printf("Only 'generic' migration is implemented. Use: doctorclaw migrate run generic <path-to.json>\n");
            return -1;
        }
        config_t cfg;
        config_load(NULL, &cfg);
        migration_manager_t mgr;
        migration_manager_init(&mgr);
        if (migration_add(&mgr, src, source_path, cfg.paths.workspace_dir) != 0) {
            printf("Failed to add migration.\n");
            return -1;
        }
        printf("Running migration from '%s' -> %s\n", source_path, cfg.paths.workspace_dir);
        int r = migration_execute(&mgr, source_path);
        migration_t *migrations = NULL;
        size_t count = 0;
        migration_list(&mgr, &migrations, &count);
        if (migrations && count > 0 && migrations[0].state == MIGRATION_STATE_COMPLETED) {
            printf("Migrated %zu items", migrations[0].items_migrated);
            if (migrations[0].items_failed > 0)
                printf(", %zu failed", migrations[0].items_failed);
            printf(".\n");
        } else if (migrations && count > 0 && migrations[0].error[0]) {
            printf("Migration failed: %s\n", migrations[0].error);
        }
        migration_manager_free(&mgr);
        return r != 0 ? -1 : 0;
    } else if (strcmp(argv[0], "run") == 0 && argc < 3) {
        printf("Usage: doctorclaw migrate run <source> <path>\n");
        printf("  e.g. migrate run generic /path/to/export.json\n");
        return -1;
    }
    
    return 0;
}

static int cmd_auth(int argc, char **argv) {
    printf("[DoctorClaw] Authentication profiles\n");
    printf("===============================\n\n");
    
    auth_t auth;
    auth_init(&auth);
    
    char *home = getenv("HOME");
    char auth_path[512];
    snprintf(auth_path, sizeof(auth_path), "%s/.doctorclaw/auth.toml", home ? home : ".");
    auth_load(&auth, auth_path);
    
    if (argc == 0) {
        printf("Usage: doctorclaw auth <command> [args]\n");
        printf("Commands:\n");
        printf("  list              List profiles\n");
        printf("  add <name> <provider> <key>  Add profile\n");
        printf("  remove <name>     Remove profile\n");
        printf("  active <name>    Set active profile\n");
        
        auth_profile_t *profiles;
        size_t count;
        auth_list_profiles(&auth, &profiles, &count);
        
        printf("\nProfiles (%zu):\n", count);
        for (size_t i = 0; i < count; i++) {
            printf("  %-15s %-15s %s\n", 
                   profiles[i].name, 
                   auth_provider_name(profiles[i].provider),
                   profiles[i].active ? "[active]" : "");
        }
        
        auth_free(&auth);
        return 0;
    }
    
    if (strcmp(argv[0], "list") == 0) {
        auth_profile_t *profiles;
        size_t count;
        auth_list_profiles(&auth, &profiles, &count);
        
        for (size_t i = 0; i < count; i++) {
            printf("  %-15s %-15s %s\n", 
                   profiles[i].name, 
                   auth_provider_name(profiles[i].provider),
                   profiles[i].active ? "[active]" : "");
        }
    } else if (strcmp(argv[0], "add") == 0 && argc >= 4) {
        auth_provider_t p = auth_provider_from_name(argv[2]);
        auth_add_profile(&auth, argv[1], p, argv[3]);
        auth_save(&auth, auth_path);
        printf("Profile '%s' added\n", argv[1]);
    } else if (strcmp(argv[0], "active") == 0 && argc >= 2) {
        auth_set_active(&auth, argv[1]);
        auth_save(&auth, auth_path);
        printf("Profile '%s' is now active\n", argv[1]);
    } else if (strcmp(argv[0], "remove") == 0 && argc >= 2) {
        if (auth_remove_profile(&auth, argv[1]) == 0) {
            auth_save(&auth, auth_path);
            printf("Profile '%s' removed\n", argv[1]);
        } else {
            printf("Profile '%s' not found\n", argv[1]);
        }
    }
    
    auth_free(&auth);
    return 0;
}

static int cmd_hardware(int argc, char **argv) {
    (void)argc;
    (void)argv;
    printf("[DoctorClaw] Hardware discovery\n");
    printf("===========================\n\n");
    
    hardware_manager_t mgr;
    hardware_init(&mgr);
    hardware_scan(&mgr);
    
    hardware_device_t *devices;
    size_t count;
    hardware_list(&mgr, &devices, &count);
    
    printf("Found %zu devices:\n\n", count);
    
    for (size_t i = 0; i < count; i++) {
        printf("  %s\n", devices[i].name);
        printf("    Path:   %s\n", devices[i].path);
        printf("    Type:   %d\n", devices[i].type);
        printf("    Status: %s\n\n", devices[i].connected ? "connected" : "disconnected");
    }
    
    if (count == 0) {
        printf("  No devices found.\n");
        printf("  (On macOS, check /dev/cu.* for serial devices)\n");
    }
    
    hardware_free(&mgr);
    return 0;
}

static int cmd_peripheral(int argc, char **argv) {
    printf("[DoctorClaw] Peripheral management\n");
    printf("===============================\n\n");
    
    if (argc == 0) {
        printf("Usage: doctorclaw peripheral <command>\n");
        printf("Commands:\n");
        printf("  list              List peripherals\n");
        printf("  scan              Scan for peripherals\n");
        return 0;
    }
    
    if (strcmp(argv[0], "list") == 0 || strcmp(argv[0], "scan") == 0) {
        printf("Scanning for peripherals...\n");
        
        hardware_manager_t mgr;
        hardware_init(&mgr);
        hardware_scan(&mgr);
        
        hardware_device_t *devices;
        size_t count;
        hardware_list(&mgr, &devices, &count);
        
        printf("Found %zu devices\n", count);
        
        hardware_free(&mgr);
    }
    
    return 0;
}

static int cmd_log(int argc, char **argv) {
    if (argc < 1) {
        fprintf(stderr, "Usage: doctorclaw log export <dest_path>\n");
        return 1;
    }

    if (strcmp(argv[0], "export") != 0) {
        fprintf(stderr, "Unknown log command: %s\n", argv[0]);
        fprintf(stderr, "Usage: doctorclaw log export <dest_path>\n");
        return 1;
    }

    if (argc < 2 || !argv[1] || !argv[1][0]) {
        fprintf(stderr, "Usage: doctorclaw log export <dest_path>\n");
        return 1;
    }

    config_t cfg;
    config_load(NULL, &cfg);
    ensure_log_dirs(&cfg);
    configure_logging_default(&cfg);

    const char *dest = argv[1];
    if (log_export(dest) != 0) {
        const char *src = log_get_file_path();
        fprintf(stderr, "Failed to export log%s%s\n", src ? " from " : "", src ? src : "");
        return 1;
    }

    printf("Exported log to %s\n", dest);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            print_version();
            return 0;
        }
        if (strcmp(argv[i], "--verbose") == 0) {
            g_verbose = true;
        }
    }
    
    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        print_usage(argv[0]);
        return 0;
    }
    
    if (strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0) {
        print_version();
        return 0;
    }
    
    command_t cmd = parse_command(argv[1]);
    
    if (cmd == CMD_UNKNOWN) {
        fprintf(stderr, "Unknown command: %s\n", argv[1]);
        print_usage(argv[0]);
        return 1;
    }
    
    int result = 0;
    int new_argc = argc - 2;
    char **new_argv = (argc > 2) ? argv + 2 : (char*[]){NULL};
    
    switch (cmd) {
        case CMD_ONBOARD:
            result = cmd_onboard(new_argc, new_argv);
            break;
        case CMD_AGENT:
            result = cmd_agent(new_argc, new_argv);
            break;
        case CMD_GATEWAY:
            result = cmd_gateway(new_argc, new_argv);
            break;
        case CMD_DAEMON:
            result = cmd_daemon(new_argc, new_argv);
            break;
        case CMD_SERVICE:
            result = cmd_service(new_argc, new_argv);
            break;
        case CMD_DOCTOR:
            result = cmd_doctor(new_argc, new_argv);
            break;
        case CMD_STATUS:
            result = cmd_status(new_argc, new_argv);
            break;
        case CMD_CRON:
            result = cmd_cron(new_argc, new_argv);
            break;
        case CMD_MODELS:
            result = cmd_models(new_argc, new_argv);
            break;
        case CMD_PROVIDERS:
            result = cmd_providers(new_argc, new_argv);
            break;
        case CMD_CHANNEL:
            result = cmd_channel(new_argc, new_argv);
            break;
        case CMD_INTEGRATIONS:
            result = cmd_integrations(new_argc, new_argv);
            break;
        case CMD_SKILLS:
            result = cmd_skills(new_argc, new_argv);
            break;
        case CMD_MIGRATE:
            result = cmd_migrate(new_argc, new_argv);
            break;
        case CMD_AUTH:
            result = cmd_auth(new_argc, new_argv);
            break;
        case CMD_HARDWARE:
            result = cmd_hardware(new_argc, new_argv);
            break;
        case CMD_PERIPHERAL:
            result = cmd_peripheral(new_argc, new_argv);
            break;
        case CMD_VERIFY_TASK_FOCUS:
            result = cmd_verify_task_focus(new_argc, new_argv);
            break;
        case CMD_LOG:
            result = cmd_log(new_argc, new_argv);
            break;
        case CMD_PENTEST: {
            const char *base = (new_argc >= 1 && new_argv[0]) ? new_argv[0] : "http://127.0.0.1:8080";
            if (providers_init() != 0) {
                fprintf(stderr, "pentest: providers init failed\n");
                result = 1;
            } else {
                result = pentest_run(base);
                providers_shutdown();
            }
            break;
        }
        case CMD_STOP:
            result = cmd_stop(new_argc, new_argv);
            break;
        case CMD_TEST:
            result = cmd_test(new_argc, new_argv);
            break;
        default:
            fprintf(stderr, "Command not implemented yet\n");
            return 1;
    }
    
    return result;
}
