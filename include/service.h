#ifndef DOCTORCLAW_SERVICE_H
#define DOCTORCLAW_SERVICE_H

#include "c23_check.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAX_SERVICE_NAME 64
#define MAX_SERVICE_PATH 512

typedef enum {
    SERVICE_STATE_UNKNOWN,
    SERVICE_STATE_STOPPED,
    SERVICE_STATE_STARTING,
    SERVICE_STATE_RUNNING,
    SERVICE_STATE_STOPPING,
    SERVICE_STATE_FAILED
} service_state_t;

typedef struct {
    char name[MAX_SERVICE_NAME];
    char executable[MAX_SERVICE_PATH];
    char args[1024];
    bool enabled;
    service_state_t state;
    int pid;
    uint64_t started_at;
    uint64_t last_exit;
    int exit_code;
} service_t;

typedef struct {
    service_t services[32];
    size_t service_count;
} service_manager_t;

int service_manager_init(service_manager_t *mgr);
int service_register(service_manager_t *mgr, const char *name, const char *executable, const char *args);
int service_unregister(service_manager_t *mgr, const char *name);
int service_start(service_manager_t *mgr, const char *name);
int service_stop(service_manager_t *mgr, const char *name);
int service_restart(service_manager_t *mgr, const char *name);
int service_enable(service_manager_t *mgr, const char *name);
int service_disable(service_manager_t *mgr, const char *name);
int service_status(service_manager_t *mgr, const char *name, service_t *out_service);
int service_list(service_manager_t *mgr, service_t **out_services, size_t *out_count);
int service_manager_save(service_manager_t *mgr, const char *path);
int service_manager_load(service_manager_t *mgr, const char *path);
void service_manager_free(service_manager_t *mgr);

const char *service_state_name(service_state_t state);

#endif
