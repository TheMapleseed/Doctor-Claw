#ifndef DOCTORCLAW_SECURITY_H
#define DOCTORCLAW_SECURITY_H

#include "c23_check.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAX_POLICY_RULES 128
#define MAX_SECRET_NAME 64
#define MAX_SECRET_VALUE 512

typedef enum {
    SECURITY_LEVEL_NONE,
    SECURITY_LEVEL_BASIC,
    SECURITY_LEVEL_SANDBOXED,
    SECURITY_LEVEL_HARDENED
} security_level_t;

typedef struct {
    bool encryption_enabled;
    bool api_key_secure;
    security_level_t level;
    bool pairing_required;
    bool audit_enabled;
} security_t;

typedef struct {
    char name[MAX_SECRET_NAME];
    char value[MAX_SECRET_VALUE];
    char path[256];
} secret_t;

typedef struct {
    char rule[512];
    bool allow;
} policy_rule_t;

typedef struct {
    policy_rule_t rules[MAX_POLICY_RULES];
    size_t rule_count;
    bool default_allow;
} security_policy_t;

/* Merkle-style chain: each entry has previous_hash and current_hash = H(previous_hash || entry) for tamper-evidence */
#define AUDIT_HASH_HEX_LEN 16
typedef struct {
    char action[64];
    char target[256];
    char user[64];
    uint64_t timestamp;
    bool allowed;
    char previous_hash[AUDIT_HASH_HEX_LEN + 1];  /* chain link */
    char current_hash[AUDIT_HASH_HEX_LEN + 1];
} audit_entry_t;

typedef struct {
    bool enabled;
    char workspace_dir[256];
    char cache_dir[256];
    bool network_allowed;
    bool subprocess_allowed;
    bool filesystem_ro;
    bool filesystem_rw;
} sandbox_config_t;

int security_init(security_t *sec);
int security_encrypt(const char *input, char *output, size_t output_size);
int security_decrypt(const char *input, char *output, size_t output_size);
bool security_validate_api_key(const char *key);

int security_policy_init(security_policy_t *policy);
int security_policy_add_rule(security_policy_t *policy, const char *rule, bool allow);
int security_policy_check(security_policy_t *policy, const char *action, const char *target);

int security_pairing_init(const char *device_name);
int security_pairing_approve(const char *pairing_code);
int security_pairing_revoke(const char *device_id);
bool security_pairing_is_paired(void);

int security_secrets_store(const char *name, const char *value);
int security_secrets_retrieve(const char *name, char *value, size_t value_size);
int security_secrets_delete(const char *name);
int security_secrets_list(char **names, size_t *count);

int security_audit_log(const char *action, const char *target, bool allowed);
int security_audit_get_entries(audit_entry_t *entries, size_t *count);
int security_audit_clear(void);

int security_sandbox_init(const sandbox_config_t *config);
int security_sandbox_enable(void);
int security_sandbox_disable(void);
int security_sandbox_exec(const char *command, char *output, size_t output_size);

int security_bubblewrap_init(const char *rootfs);
int security_bubblewrap_run(const char *command, const char **args);
int security_bubblewrap_cleanup(void);

int security_firejail_init(const char *profile);
int security_firejail_run(const char *command);
int security_firejail_cleanup(void);

int security_landlock_init(void);
int security_landlock_restrict_path(const char *path, int flags);
int security_landlock_apply(void);

int security_docker_sandbox_init(const char *image, const char *container_name);
int security_docker_sandbox_run(const char *command, char *output, size_t output_size);
int security_docker_sandbox_stop(void);
int security_docker_sandbox_cleanup(void);

int security_detect_container(void);
int security_detect_sandbox(void);

#endif
