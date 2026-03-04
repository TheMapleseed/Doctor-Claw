#ifndef DOCTORCLAW_DAEMON_H
#define DOCTORCLAW_DAEMON_H

#include "c23_check.h"
#include <stdint.h>
#include <stdbool.h>
#include "config.h"

#define MAX_DAEMON_COMPONENTS 16

typedef enum {
    DAEMON_STATE_STOPPED,
    DAEMON_STATE_STARTING,
    DAEMON_STATE_RUNNING,
    DAEMON_STATE_STOPPING,
    DAEMON_STATE_FAILED
} daemon_state_t;

typedef struct {
    char name[64];
    daemon_state_t state;
    uint64_t started_at;
    uint64_t last_heartbeat;
    bool healthy;
    char error[256];
} daemon_component_t;

typedef struct {
    daemon_state_t state;
    daemon_component_t components[MAX_DAEMON_COMPONENTS];
    size_t component_count;
    uint64_t started_at;
    uint64_t uptime_seconds;
    char pid_file[256];
    char log_file[256];
} daemon_context_t;

int daemon_init(daemon_context_t *ctx);
int daemon_start(daemon_context_t *ctx, config_t *config, const char *host, uint16_t port);
int daemon_stop(daemon_context_t *ctx);
int daemon_restart(daemon_context_t *ctx);
int daemon_status(daemon_context_t *ctx);
int daemon_register_component(daemon_context_t *ctx, const char *name);
int daemon_update_component(daemon_context_t *ctx, const char *name, daemon_state_t state, bool healthy, const char *error);
daemon_state_t daemon_get_state(daemon_context_t *ctx);
void daemon_free(daemon_context_t *ctx);

int daemon_run(config_t *config, const char *host, uint16_t port);

#endif
