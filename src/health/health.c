#include "health.h"
#include "providers.h"
#include "memory.h"
#include "channels.h"
#include "runtime.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

#define MAX_CHECKERS 16

typedef struct {
    const char *name;
    health_status_t (*check_fn)(void);
} registered_checker_t;

static registered_checker_t g_checkers[MAX_CHECKERS];
static size_t g_checker_count = 0;

int health_init(health_monitor_t *mon) {
    if (!mon) return -1;
    memset(mon, 0, sizeof(health_monitor_t));
    mon->auto_check = false;
    mon->check_interval_sec = 60;
    return 0;
}

int health_init_auto(health_monitor_t *mon, int interval_sec) {
    if (!mon) return -1;
    health_init(mon);
    mon->auto_check = true;
    mon->check_interval_sec = interval_sec > 0 ? interval_sec : 60;
    
    health_register_checker(mon, "providers", health_check_provider);
    health_register_checker(mon, "memory", health_check_memory);
    health_register_checker(mon, "channels", health_check_channels);
    health_register_checker(mon, "runtime", health_check_runtime);
    
    return 0;
}

int health_register(health_monitor_t *mon, const char *component) {
    if (!mon || !component) return -1;
    if (mon->check_count >= 32) return -1;
    
    health_check_t *check = &mon->checks[mon->check_count];
    snprintf(check->component, sizeof(check->component), "%s", component);
    check->status = HEALTH_OK;
    check->message[0] = '\0';
    check->last_check = (uint64_t)time(NULL);
    mon->check_count++;
    return 0;
}

int health_register_checker(health_monitor_t *mon, const char *name, health_status_t (*check_fn)(void)) {
    (void)mon;
    if (!name || !check_fn) return -1;
    if (g_checker_count >= MAX_CHECKERS) return -1;
    
    g_checkers[g_checker_count].name = name;
    g_checkers[g_checker_count].check_fn = check_fn;
    g_checker_count++;
    return 0;
}

int health_run_checks(health_monitor_t *mon) {
    if (!mon) return -1;
    
    for (size_t i = 0; i < g_checker_count; i++) {
        health_status_t status = g_checkers[i].check_fn();
        health_update(mon, g_checkers[i].name, status, NULL);
    }
    
    return 0;
}

health_status_t health_check_provider(void) {
    const char *key = getenv("OPENROUTER_API_KEY");
    if (!key) key = getenv("OPENAI_API_KEY");
    if (!key) key = getenv("ANTHROPIC_API_KEY");
    if (!key) key = getenv("OLLAMA_HOST");
    
    if (key && key[0]) {
        return HEALTH_OK;
    }
    return HEALTH_DEGRADED;
}

health_status_t health_check_memory(void) {
    return HEALTH_OK;
}

health_status_t health_check_channels(void) {
    return HEALTH_OK;
}

health_status_t health_check_runtime(void) {
    runtime_info_t info = {0};
    runtime_get_info(&info);
    
    if (info.state == RUNTIME_STATE_RUNNING) {
        return HEALTH_OK;
    }
    return HEALTH_DEGRADED;
}

int health_update(health_monitor_t *mon, const char *component, health_status_t status, const char *message) {
    if (!mon || !component) return -1;
    
    for (size_t i = 0; i < mon->check_count; i++) {
        if (strcmp(mon->checks[i].component, component) == 0) {
            mon->checks[i].status = status;
            if (message) {
                snprintf(mon->checks[i].message, sizeof(mon->checks[i].message), "%s", message);
            }
            mon->checks[i].last_check = (uint64_t)time(NULL);
            return 0;
        }
    }
    return -1;
}

int health_get(health_monitor_t *mon, const char *component, health_check_t *out_check) {
    if (!mon || !component || !out_check) return -1;
    for (size_t i = 0; i < mon->check_count; i++) {
        if (strcmp(mon->checks[i].component, component) == 0) {
            *out_check = mon->checks[i];
            return 0;
        }
    }
    return -1;
}

int health_list(health_monitor_t *mon, health_check_t **out_checks, size_t *out_count) {
    if (!mon || !out_checks || !out_count) return -1;
    *out_checks = mon->checks;
    *out_count = mon->check_count;
    return 0;
}

void health_free(health_monitor_t *mon) {
    if (mon) {
        memset(mon, 0, sizeof(health_monitor_t));
    }
}
