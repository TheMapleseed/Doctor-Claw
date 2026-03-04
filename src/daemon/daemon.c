#include "daemon.h"
#include "channels.h"
#include "gateway.h"
#include "memory.h"
#include "providers.h"
#include "health.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

static volatile sig_atomic_t g_running = 1;
static daemon_context_t *g_daemon_ctx = NULL;

static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
    if (g_daemon_ctx) {
        g_daemon_ctx->state = DAEMON_STATE_STOPPING;
    }
}

int daemon_init(daemon_context_t *ctx) {
    if (!ctx) return -1;
    memset(ctx, 0, sizeof(daemon_context_t));
    ctx->state = DAEMON_STATE_STOPPED;
    ctx->component_count = 0;
    ctx->pid_file[0] = '\0';
    ctx->log_file[0] = '\0';
    return 0;
}

int daemon_start(daemon_context_t *ctx, config_t *config, const char *host, uint16_t port) {
    if (!ctx || !config) return -1;
    
    ctx->state = DAEMON_STATE_STARTING;
    ctx->started_at = (uint64_t)time(NULL);
    
    daemon_register_component(ctx, "providers");
    daemon_register_component(ctx, "memory");
    daemon_register_component(ctx, "gateway");
    daemon_register_component(ctx, "channels");
    
    providers_init();
    daemon_update_component(ctx, "providers", DAEMON_STATE_RUNNING, true, NULL);
    
    memory_t mem = {0};
    if (config->memory.backend[0]) {
        char db_path[512];
        snprintf(db_path, sizeof(db_path), "%s/memory.db", config->paths.data_dir);
        memory_init(&mem, MEMORY_BACKEND_SQLITE, db_path);
    }
    daemon_update_component(ctx, "memory", DAEMON_STATE_RUNNING, true, NULL);
    
    daemon_update_component(ctx, "gateway", DAEMON_STATE_RUNNING, true, NULL);
    
    daemon_update_component(ctx, "channels", DAEMON_STATE_RUNNING, true, NULL);
    
    ctx->state = DAEMON_STATE_RUNNING;
    
    printf("[Daemon] Doctor Claw Daemon started successfully\n");
    printf("[Daemon] Gateway listening on http://%s:%d\n", host, port);
    
    return 0;
}

int daemon_stop(daemon_context_t *ctx) {
    if (!ctx) return -1;
    
    ctx->state = DAEMON_STATE_STOPPING;
    
    for (size_t i = 0; i < ctx->component_count; i++) {
        ctx->components[i].state = DAEMON_STATE_STOPPING;
    }
    
    printf("[Daemon] Stopping components...\n");
    
    for (size_t i = 0; i < ctx->component_count; i++) {
        ctx->components[i].state = DAEMON_STATE_STOPPED;
    }
    
    providers_shutdown();
    
    ctx->state = DAEMON_STATE_STOPPED;
    printf("[Daemon] Daemon stopped\n");
    
    return 0;
}

int daemon_restart(daemon_context_t *ctx) {
    if (!ctx) return -1;
    daemon_stop(ctx);
    return daemon_start(ctx, NULL, "0.0.0.0", 8080);
}

int daemon_status(daemon_context_t *ctx) {
    if (!ctx) return -1;
    
    printf("Doctor Claw Daemon Status\n");
    printf("=========================\n\n");
    
    const char *state_name = "unknown";
    switch (ctx->state) {
        case DAEMON_STATE_STOPPED: state_name = "Stopped"; break;
        case DAEMON_STATE_STARTING: state_name = "Starting"; break;
        case DAEMON_STATE_RUNNING: state_name = "Running"; break;
        case DAEMON_STATE_STOPPING: state_name = "Stopping"; break;
        case DAEMON_STATE_FAILED: state_name = "Failed"; break;
    }
    
    printf("State: %s\n", state_name);
    
    uint64_t uptime = 0;
    if (ctx->state == DAEMON_STATE_RUNNING && ctx->started_at > 0) {
        uptime = (uint64_t)time(NULL) - ctx->started_at;
    }
    printf("Uptime: %lu seconds\n", (unsigned long)uptime);
    
    printf("\nComponents (%zu):\n", ctx->component_count);
    for (size_t i = 0; i < ctx->component_count; i++) {
        daemon_component_t *c = &ctx->components[i];
        const char *cstate = c->state == DAEMON_STATE_RUNNING ? "running" : 
                             c->state == DAEMON_STATE_STOPPING ? "stopping" : "stopped";
        printf("  %-15s %-10s %s\n", c->name, cstate, c->healthy ? "[OK]" : "[FAILED]");
    }
    
    return 0;
}

int daemon_register_component(daemon_context_t *ctx, const char *name) {
    if (!ctx || !name) return -1;
    if (ctx->component_count >= MAX_DAEMON_COMPONENTS) return -1;
    
    daemon_component_t *c = &ctx->components[ctx->component_count];
    snprintf(c->name, sizeof(c->name), "%s", name);
    c->state = DAEMON_STATE_STOPPED;
    c->healthy = false;
    c->started_at = 0;
    c->last_heartbeat = 0;
    c->error[0] = '\0';
    
    ctx->component_count++;
    return 0;
}

int daemon_update_component(daemon_context_t *ctx, const char *name, daemon_state_t state, bool healthy, const char *error) {
    if (!ctx || !name) return -1;
    
    for (size_t i = 0; i < ctx->component_count; i++) {
        if (strcmp(ctx->components[i].name, name) == 0) {
            ctx->components[i].state = state;
            ctx->components[i].healthy = healthy;
            ctx->components[i].last_heartbeat = (uint64_t)time(NULL);
            if (state == DAEMON_STATE_RUNNING && ctx->components[i].started_at == 0) {
                ctx->components[i].started_at = ctx->components[i].last_heartbeat;
            }
            if (error) {
                snprintf(ctx->components[i].error, sizeof(ctx->components[i].error), "%s", error);
            }
            return 0;
        }
    }
    return -1;
}

daemon_state_t daemon_get_state(daemon_context_t *ctx) {
    if (!ctx) return DAEMON_STATE_FAILED;
    return ctx->state;
}

void daemon_free(daemon_context_t *ctx) {
    if (ctx) {
        memset(ctx, 0, sizeof(daemon_context_t));
    }
}

int daemon_run(config_t *config, const char *host, uint16_t port) {
    printf("[Daemon] Starting Doctor Claw Daemon...\n");
    printf("[Daemon] Workspace: %s\n", config->paths.workspace_dir);
    printf("[Daemon] Gateway: %s:%d\n", host, port);
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    daemon_context_t ctx;
    daemon_init(&ctx);
    g_daemon_ctx = &ctx;
    
    daemon_start(&ctx, config, host, port);
    
    gateway_run(host, port, config);
    
    while (g_running && ctx.state == DAEMON_STATE_RUNNING) {
        sleep(1);
        
        ctx.uptime_seconds = (uint64_t)time(NULL) - ctx.started_at;
    }
    
    printf("[Daemon] Shutting down...\n");
    daemon_stop(&ctx);
    daemon_free(&ctx);
    g_daemon_ctx = NULL;
    
    return 0;
}
