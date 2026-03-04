#include "onboard.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

static const char *state_names[] = {
    "not_started",
    "in_progress",
    "completed",
    "failed"
};

int onboard_init(onboard_t *onboard) {
    if (!onboard) return -1;
    memset(onboard, 0, sizeof(onboard_t));
    onboard->state = ONBOARD_STATE_NOT_STARTED;
    onboard->total_steps = 5;
    return 0;
}

static int onboard_create_config(onboard_t *onboard) {
    char config_path[1024];
    snprintf(config_path, sizeof(config_path), "%s/config.toml", onboard->workspace_path);
    
    FILE *f = fopen(config_path, "w");
    if (!f) return -1;
    
    fprintf(f, "# DoctorClaw Configuration\n\n");
    fprintf(f, "[doctorclaw]\n");
    fprintf(f, "version = \"0.1.0\"\n");
    fprintf(f, "workspace = \"%s\"\n\n", onboard->workspace_path);
    fprintf(f, "[providers]\n");
    fprintf(f, "default = \"openrouter\"\n\n");
    fprintf(f, "[memory]\n");
    fprintf(f, "backend = \"sqlite\"\n");
    fprintf(f, "path = \"%s/memory.db\"\n\n", onboard->workspace_path);
    
    fclose(f);
    onboard->config_created = true;
    onboard->steps_completed++;
    return 0;
}

static int onboard_create_directories(onboard_t *onboard) {
    char dir_path[1024];
    
    snprintf(dir_path, sizeof(dir_path), "%s/.doctorclaw", onboard->workspace_path);
    mkdir(dir_path, 0755);
    
    snprintf(dir_path, sizeof(dir_path), "%s/.doctorclaw/channels", onboard->workspace_path);
    mkdir(dir_path, 0755);
    
    snprintf(dir_path, sizeof(dir_path), "%s/.doctorclaw/skills", onboard->workspace_path);
    mkdir(dir_path, 0755);
    
    onboard->steps_completed++;
    return 0;
}

static int onboard_setup_auth(onboard_t *onboard) {
    char auth_path[1024];
    snprintf(auth_path, sizeof(auth_path), "%s/.doctorclaw/auth.toml", onboard->workspace_path);
    
    FILE *f = fopen(auth_path, "w");
    if (!f) return -1;
    
    fprintf(f, "# DoctorClaw Authentication Profiles\n\n");
    fprintf(f, "# Add your API keys below:\n");
    fprintf(f, "# Example:\n");
    fprintf(f, "# [profile]\n");
    fprintf(f, "# name = \"openai\"\n");
    fprintf(f, "# provider = \"openai\"\n");
    fprintf(f, "# api_key = \"sk-...\"\n\n");
    
    fclose(f);
    onboard->auth_configured = true;
    onboard->steps_completed++;
    return 0;
}

static int onboard_setup_channels(onboard_t *onboard) {
    char channels_path[1024];
    snprintf(channels_path, sizeof(channels_path), "%s/.doctorclaw/channels/config.toml", onboard->workspace_path);
    
    FILE *f = fopen(channels_path, "w");
    if (!f) return -1;
    
    fprintf(f, "# DoctorClaw Channel Configuration\n\n");
    fprintf(f, "# Uncomment and configure your channels:\n\n");
    fprintf(f, "# [telegram]\n");
    fprintf(f, "# enabled = false\n");
    fprintf(f, "# bot_token = \"\"\n\n");
    fprintf(f, "# [discord]\n");
    fprintf(f, "# enabled = false\n");
    fprintf(f, "# bot_token = \"\"\n\n");
    fprintf(f, "# [slack]\n");
    fprintf(f, "# enabled = false\n");
    fprintf(f, "# bot_token = \"\"\n");
    
    fclose(f);
    onboard->channels_configured = true;
    onboard->steps_completed++;
    return 0;
}

static int onboard_load_skills(onboard_t *onboard) {
    onboard->skills_loaded = true;
    onboard->steps_completed++;
    return 0;
}

int onboard_execute(onboard_t *onboard, const char *workspace_path) {
    if (!onboard || !workspace_path) return -1;
    
    snprintf(onboard->workspace_path, sizeof(onboard->workspace_path), "%s", workspace_path);
    onboard->state = ONBOARD_STATE_IN_PROGRESS;
    
    printf("Starting DoctorClaw onboarding...\n");
    printf("Workspace: %s\n\n", workspace_path);
    
    if (onboard_create_directories(onboard) != 0) {
        onboard->state = ONBOARD_STATE_FAILED;
        snprintf(onboard->error, sizeof(onboard->error), "Failed to create directories");
        return -1;
    }
    printf("[1/%d] Created configuration directories\n", onboard->total_steps);
    
    if (onboard_create_config(onboard) != 0) {
        onboard->state = ONBOARD_STATE_FAILED;
        snprintf(onboard->error, sizeof(onboard->error), "Failed to create config");
        return -1;
    }
    printf("[2/%d] Created configuration file\n", onboard->total_steps);
    
    if (onboard_setup_auth(onboard) != 0) {
        onboard->state = ONBOARD_STATE_FAILED;
        snprintf(onboard->error, sizeof(onboard->error), "Failed to setup auth");
        return -1;
    }
    printf("[3/%d] Created auth configuration\n", onboard->total_steps);
    
    if (onboard_setup_channels(onboard) != 0) {
        onboard->state = ONBOARD_STATE_FAILED;
        snprintf(onboard->error, sizeof(onboard->error), "Failed to setup channels");
        return -1;
    }
    printf("[4/%d] Created channel configuration\n", onboard->total_steps);
    
    if (onboard_load_skills(onboard) != 0) {
        onboard->state = ONBOARD_STATE_FAILED;
        snprintf(onboard->error, sizeof(onboard->error), "Failed to load skills");
        return -1;
    }
    printf("[5/%d] Skills system initialized\n", onboard->total_steps);
    
    onboard->state = ONBOARD_STATE_COMPLETED;
    printf("\nOnboarding completed successfully!\n");
    printf("Next steps:\n");
    printf("  1. Edit ~/.doctorclaw/auth.toml to add your API keys\n");
    printf("  2. Configure channels in ~/.doctorclaw/channels/config.toml\n");
    printf("  3. Run: doctorclaw agent\n");
    
    return 0;
}

int onboard_status(onboard_t *onboard, onboard_state_t *out_state) {
    if (!onboard || !out_state) return -1;
    *out_state = onboard->state;
    return 0;
}

int onboard_reset(onboard_t *onboard) {
    if (!onboard) return -1;
    memset(onboard, 0, sizeof(onboard_t));
    onboard->state = ONBOARD_STATE_NOT_STARTED;
    onboard->total_steps = 5;
    return 0;
}

void onboard_free(onboard_t *onboard) {
    if (onboard) {
        memset(onboard, 0, sizeof(onboard_t));
    }
}

const char *onboard_state_name(onboard_state_t state) {
    if (state >= 0 && state <= ONBOARD_STATE_FAILED) {
        return state_names[state];
    }
    return "unknown";
}
