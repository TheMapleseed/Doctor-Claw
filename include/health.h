#ifndef DOCTORCLAW_HEALTH_H
#define DOCTORCLAW_HEALTH_H

#include "c23_check.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    HEALTH_OK,
    HEALTH_DEGRADED,
    HEALTH_FAILED
} health_status_t;

typedef struct {
    char component[64];
    health_status_t status;
    char message[256];
    uint64_t last_check;
} health_check_t;

typedef struct {
    health_check_t checks[32];
    size_t check_count;
    bool auto_check;
    int check_interval_sec;
} health_monitor_t;

typedef struct {
    const char *name;
    health_status_t (*check_fn)(void);
} health_checker_t;

int health_init(health_monitor_t *mon);
int health_init_auto(health_monitor_t *mon, int interval_sec);
int health_register(health_monitor_t *mon, const char *component);
int health_register_checker(health_monitor_t *mon, const char *name, health_status_t (*check_fn)(void));
int health_run_checks(health_monitor_t *mon);
int health_update(health_monitor_t *mon, const char *component, health_status_t status, const char *message);
int health_get(health_monitor_t *mon, const char *component, health_check_t *out_check);
int health_list(health_monitor_t *mon, health_check_t **out_checks, size_t *out_count);
void health_free(health_monitor_t *mon);

health_status_t health_check_provider(void);
health_status_t health_check_memory(void);
health_status_t health_check_channels(void);
health_status_t health_check_runtime(void);

#endif
