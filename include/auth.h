#ifndef DOCTORCLAW_AUTH_H
#define DOCTORCLAW_AUTH_H

#include "c23_check.h"
#include <stdbool.h>
#include <stddef.h>

#define MAX_AUTH_PROFILES 16
#define MAX_PROFILE_NAME 64
#define MAX_API_KEY 256

typedef enum {
    AUTH_PROVIDER_OPENAI,
    AUTH_PROVIDER_ANTHROPIC,
    AUTH_PROVIDER_OPENROUTER,
    AUTH_PROVIDER_GEMINI,
    AUTH_PROVIDER_GLM,
    AUTH_PROVIDER_OLLAMA,
    AUTH_PROVIDER_LLAMACPP,
    AUTH_PROVIDER_NONE
} auth_provider_t;

typedef struct {
    char name[MAX_PROFILE_NAME];
    auth_provider_t provider;
    char api_key[MAX_API_KEY];
    char endpoint[512];
    bool active;
} auth_profile_t;

typedef struct {
    auth_profile_t profiles[MAX_AUTH_PROFILES];
    size_t profile_count;
    char active_profile[MAX_PROFILE_NAME];
} auth_t;

int auth_init(auth_t *auth);
int auth_add_profile(auth_t *auth, const char *name, auth_provider_t provider, const char *api_key);
int auth_remove_profile(auth_t *auth, const char *name);
int auth_set_active(auth_t *auth, const char *name);
int auth_get_active(auth_t *auth, auth_profile_t *out_profile);
int auth_list_profiles(auth_t *auth, auth_profile_t **out_profiles, size_t *out_count);
int auth_save(auth_t *auth, const char *path);
int auth_load(auth_t *auth, const char *path);
void auth_free(auth_t *auth);

const char *auth_provider_name(auth_provider_t provider);
auth_provider_t auth_provider_from_name(const char *name);

#endif
