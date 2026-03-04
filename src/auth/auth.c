#include "auth.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *provider_names[] = {
    "openai",
    "anthropic",
    "openrouter",
    "gemini",
    "glm",
    "ollama",
    "llamacpp",
    NULL
};

int auth_init(auth_t *auth) {
    if (!auth) return -1;
    memset(auth, 0, sizeof(auth_t));
    return 0;
}

int auth_add_profile(auth_t *auth, const char *name, auth_provider_t provider, const char *api_key) {
    if (!auth || !name || !api_key) return -1;
    if (auth->profile_count >= MAX_AUTH_PROFILES) return -1;
    
    auth_profile_t *profile = &auth->profiles[auth->profile_count];
    snprintf(profile->name, sizeof(profile->name), "%s", name);
    profile->provider = provider;
    snprintf(profile->api_key, sizeof(profile->api_key), "%s", api_key);
    profile->endpoint[0] = '\0';
    profile->active = false;
    
    auth->profile_count++;
    return 0;
}

int auth_remove_profile(auth_t *auth, const char *name) {
    if (!auth || !name) return -1;
    
    for (size_t i = 0; i < auth->profile_count; i++) {
        if (strcmp(auth->profiles[i].name, name) == 0) {
            for (size_t j = i; j < auth->profile_count - 1; j++) {
                auth->profiles[j] = auth->profiles[j + 1];
            }
            auth->profile_count--;
            return 0;
        }
    }
    return -1;
}

int auth_set_active(auth_t *auth, const char *name) {
    if (!auth || !name) return -1;
    
    for (size_t i = 0; i < auth->profile_count; i++) {
        auth->profiles[i].active = (strcmp(auth->profiles[i].name, name) == 0);
        if (auth->profiles[i].active) {
            snprintf(auth->active_profile, sizeof(auth->active_profile), "%s", name);
        }
    }
    return 0;
}

int auth_get_active(auth_t *auth, auth_profile_t *out_profile) {
    if (!auth || !out_profile) return -1;
    
    for (size_t i = 0; i < auth->profile_count; i++) {
        if (auth->profiles[i].active) {
            *out_profile = auth->profiles[i];
            return 0;
        }
    }
    return -1;
}

int auth_list_profiles(auth_t *auth, auth_profile_t **out_profiles, size_t *out_count) {
    if (!auth || !out_profiles || !out_count) return -1;
    *out_profiles = auth->profiles;
    *out_count = auth->profile_count;
    return 0;
}

int auth_save(auth_t *auth, const char *path) {
    if (!auth || !path) return -1;
    
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    
    fprintf(f, "# DoctorClaw Auth Profiles\n\n");
    fprintf(f, "active_profile: %s\n\n", auth->active_profile);
    
    for (size_t i = 0; i < auth->profile_count; i++) {
        auth_profile_t *p = &auth->profiles[i];
        fprintf(f, "[profile]\n");
        fprintf(f, "name = %s\n", p->name);
        fprintf(f, "provider = %s\n", auth_provider_name(p->provider));
        fprintf(f, "api_key = %s\n", p->api_key);
        if (p->endpoint[0]) {
            fprintf(f, "endpoint = %s\n", p->endpoint);
        }
        fprintf(f, "\n");
    }
    
    fclose(f);
    return 0;
}

int auth_load(auth_t *auth, const char *path) {
    if (!auth || !path) return -1;
    
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    
    char line[1024];
    char current_name[MAX_PROFILE_NAME] = {0};
    auth_provider_t current_provider = AUTH_PROVIDER_NONE;
    char current_key[MAX_API_KEY] = {0};
    char current_endpoint[512] = {0};
    
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        
        char key[128], val[512];
        if (sscanf(line, "%127[^=] = %511[^\n]", key, val) == 2) {
            char *k = key;
            while (*k == ' ') k++;
            
            if (strcmp(k, "active_profile") == 0) {
                snprintf(auth->active_profile, sizeof(auth->active_profile), "%s", val);
            } else if (strcmp(k, "name") == 0) {
                snprintf(current_name, sizeof(current_name), "%s", val);
            } else if (strcmp(k, "provider") == 0) {
                current_provider = auth_provider_from_name(val);
            } else if (strcmp(k, "api_key") == 0) {
                snprintf(current_key, sizeof(current_key), "%s", val);
            } else if (strcmp(k, "endpoint") == 0) {
                snprintf(current_endpoint, sizeof(current_endpoint), "%s", val);
            }
        }
    }
    
    if (current_name[0] && current_key[0]) {
        auth_add_profile(auth, current_name, current_provider, current_key);
        if (current_endpoint[0]) {
            auth_profile_t *p = &auth->profiles[auth->profile_count - 1];
            snprintf(p->endpoint, sizeof(p->endpoint), "%s", current_endpoint);
        }
        auth_set_active(auth, auth->active_profile);
    }
    
    fclose(f);
    return 0;
}

void auth_free(auth_t *auth) {
    if (auth) {
        memset(auth, 0, sizeof(auth_t));
    }
}

const char *auth_provider_name(auth_provider_t provider) {
    if (provider >= 0 && provider < AUTH_PROVIDER_NONE) {
        return provider_names[provider];
    }
    return "unknown";
}

auth_provider_t auth_provider_from_name(const char *name) {
    if (!name) return AUTH_PROVIDER_NONE;
    
    for (int i = 0; provider_names[i]; i++) {
        if (strcmp(name, provider_names[i]) == 0) {
            return (auth_provider_t)i;
        }
    }
    return AUTH_PROVIDER_NONE;
}
