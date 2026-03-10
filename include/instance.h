#ifndef DOCTORCLAW_INSTANCE_H
#define DOCTORCLAW_INSTANCE_H

#include "config.h"
#include "c23_check.h"
#include <stddef.h>

#define INSTANCE_ID_MAX 64
#define INSTANCE_REGISTRY_MAX 16

/** Initialize the instance registry (call at daemon start). */
void instance_init(void);

/** Register or update config for an instance id. Returns 0 on success, -1 if registry full. */
int instance_register(const char *instance_id, const config_t *config);

/**
 * Get config for an instance. Used by job workers to resolve instance_id -> config.
 * Returns 0 if found and config_out filled; -1 if not found.
 */
int instance_get_config(const char *instance_id, config_t *config_out);

/** Clear the registry (e.g. at daemon shutdown). */
void instance_shutdown(void);

#endif
