#include "instance.h"
#include <string.h>

static struct {
    char id[INSTANCE_REGISTRY_MAX][INSTANCE_ID_MAX];
    config_t config[INSTANCE_REGISTRY_MAX];
    size_t count;
} g_registry = {0};

void instance_init(void) {
    memset(&g_registry, 0, sizeof(g_registry));
}

int instance_register(const char *instance_id, const config_t *config) {
    if (!instance_id || !config) return -1;
    size_t i;
    for (i = 0; i < g_registry.count; i++) {
        if (strcmp(g_registry.id[i], instance_id) == 0) {
            memcpy(&g_registry.config[i], config, sizeof(config_t));
            return 0;
        }
    }
    if (g_registry.count >= INSTANCE_REGISTRY_MAX) return -1;
    size_t len = strlen(instance_id);
    if (len >= INSTANCE_ID_MAX) len = INSTANCE_ID_MAX - 1;
    memcpy(g_registry.id[g_registry.count], instance_id, len + 1);
    memcpy(&g_registry.config[g_registry.count], config, sizeof(config_t));
    g_registry.count++;
    return 0;
}

int instance_get_config(const char *instance_id, config_t *config_out) {
    if (!instance_id || !config_out) return -1;
    const char *lookup = instance_id[0] ? instance_id : "default";
    for (size_t i = 0; i < g_registry.count; i++) {
        if (strcmp(g_registry.id[i], lookup) == 0) {
            memcpy(config_out, &g_registry.config[i], sizeof(config_t));
            return 0;
        }
    }
    return -1;
}

void instance_shutdown(void) {
    memset(&g_registry, 0, sizeof(g_registry));
}
