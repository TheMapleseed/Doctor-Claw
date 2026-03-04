#ifndef DOCTORCLAW_ONBOARD_H
#define DOCTORCLAW_ONBOARD_H

#include "c23_check.h"
#include <stdbool.h>
#include <stddef.h>

#define MAX_WORKSPACE_PATH 512
#define MAX_CONFIG_KEY 64
#define MAX_CONFIG_VAL 256

typedef enum {
    ONBOARD_STATE_NOT_STARTED,
    ONBOARD_STATE_IN_PROGRESS,
    ONBOARD_STATE_COMPLETED,
    ONBOARD_STATE_FAILED
} onboard_state_t;

typedef struct {
    char workspace_path[MAX_WORKSPACE_PATH];
    onboard_state_t state;
    bool config_created;
    bool auth_configured;
    bool channels_configured;
    bool skills_loaded;
    int steps_completed;
    int total_steps;
    char error[512];
} onboard_t;

int onboard_init(onboard_t *onboard);
int onboard_execute(onboard_t *onboard, const char *workspace_path);
int onboard_status(onboard_t *onboard, onboard_state_t *out_state);
int onboard_reset(onboard_t *onboard);
void onboard_free(onboard_t *onboard);

const char *onboard_state_name(onboard_state_t state);

#endif
