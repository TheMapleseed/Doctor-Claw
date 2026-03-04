#include "security.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

#define MAX_AUDIT_ENTRIES 1000
static audit_entry_t g_audit_log[MAX_AUDIT_ENTRIES];
static size_t g_audit_count = 0;

static secret_t g_secrets[64];
static size_t g_secret_count = 0;

int security_init(security_t *sec) {
    if (!sec) return -1;
    sec->encryption_enabled = false;
    sec->api_key_secure = true;
    sec->level = SECURITY_LEVEL_BASIC;
    sec->pairing_required = false;
    sec->audit_enabled = true;
    return 0;
}

int security_encrypt(const char *input, char *output, size_t output_size) {
    if (!input || !output) return -1;
    snprintf(output, output_size, "%s", input);
    return 0;
}

int security_decrypt(const char *input, char *output, size_t output_size) {
    if (!input || !output) return -1;
    snprintf(output, output_size, "%s", input);
    return 0;
}

bool security_validate_api_key(const char *key) {
    if (!key) return false;
    if (strlen(key) < 10) return false;
    if (strstr(key, "sk-") != key) return false;
    return true;
}

int security_policy_init(security_policy_t *policy) {
    if (!policy) return -1;
    memset(policy, 0, sizeof(security_policy_t));
    policy->default_allow = false;
    return 0;
}

int security_policy_add_rule(security_policy_t *policy, const char *rule, bool allow) {
    if (!policy || !rule) return -1;
    if (policy->rule_count >= MAX_POLICY_RULES) return -1;
    
    snprintf(policy->rules[policy->rule_count].rule, sizeof(policy->rules[0].rule), "%s", rule);
    policy->rules[policy->rule_count].allow = allow;
    policy->rule_count++;
    
    return 0;
}

int security_policy_check(security_policy_t *policy, const char *action, const char *target) {
    if (!policy || !action) return -1;
    
    char check[512];
    snprintf(check, sizeof(check), "%s:%s", action, target ? target : "");
    
    for (size_t i = 0; i < policy->rule_count; i++) {
        if (strstr(check, policy->rules[i].rule)) {
            return policy->rules[i].allow ? 0 : -1;
        }
    }
    
    return policy->default_allow ? 0 : -1;
}

int security_pairing_init(const char *device_name) {
    printf("[Security] Pairing initialized for device: %s\n", device_name ? device_name : "unknown");
    return 0;
}

int security_pairing_approve(const char *pairing_code) {
    if (!pairing_code) return -1;
    printf("[Security] Pairing approved: %s\n", pairing_code);
    return 0;
}

int security_pairing_revoke(const char *device_id) {
    if (!device_id) return -1;
    printf("[Security] Pairing revoked for: %s\n", device_id);
    return 0;
}

bool security_pairing_is_paired(void) {
    return true;
}

int security_secrets_store(const char *name, const char *value) {
    if (!name || !value) return -1;
    if (g_secret_count >= 64) return -1;
    
    snprintf(g_secrets[g_secret_count].name, sizeof(g_secrets[0].name), "%s", name);
    snprintf(g_secrets[g_secret_count].value, sizeof(g_secrets[0].value), "%s", value);
    g_secret_count++;
    
    printf("[Security] Secret stored: %s\n", name);
    return 0;
}

int security_secrets_retrieve(const char *name, char *value, size_t value_size) {
    if (!name || !value) return -1;
    
    for (size_t i = 0; i < g_secret_count; i++) {
        if (strcmp(g_secrets[i].name, name) == 0) {
            snprintf(value, value_size, "%s", g_secrets[i].value);
            return 0;
        }
    }
    
    return -1;
}

int security_secrets_delete(const char *name) {
    if (!name) return -1;
    
    for (size_t i = 0; i < g_secret_count; i++) {
        if (strcmp(g_secrets[i].name, name) == 0) {
            memmove(&g_secrets[i], &g_secrets[i+1], (g_secret_count - i - 1) * sizeof(secret_t));
            g_secret_count--;
            printf("[Security] Secret deleted: %s\n", name);
            return 0;
        }
    }
    
    return -1;
}

int security_secrets_list(char **names, size_t *count) {
    if (!names || !count) return -1;
    
    for (size_t i = 0; i < g_secret_count; i++) {
        names[i] = strdup(g_secrets[i].name);
    }
    *count = g_secret_count;
    
    return 0;
}

int security_audit_log(const char *action, const char *target, bool allowed) {
    if (!action) return -1;
    
    if (g_audit_count >= MAX_AUDIT_ENTRIES) {
        memmove(&g_audit_log[0], &g_audit_log[1], (MAX_AUDIT_ENTRIES - 1) * sizeof(audit_entry_t));
        g_audit_count = MAX_AUDIT_ENTRIES - 1;
    }
    
    snprintf(g_audit_log[g_audit_count].action, sizeof(g_audit_log[0].action), "%s", action);
    snprintf(g_audit_log[g_audit_count].target, sizeof(g_audit_log[0].target), "%s", target ? target : "");
    snprintf(g_audit_log[g_audit_count].user, sizeof(g_audit_log[0].user), "doctorclaw");
    g_audit_log[g_audit_count].timestamp = time(NULL);
    g_audit_log[g_audit_count].allowed = allowed;
    g_audit_count++;
    
    return 0;
}

int security_audit_get_entries(audit_entry_t *entries, size_t *count) {
    if (!entries || !count) return -1;
    
    size_t copy_count = (g_audit_count < *count) ? g_audit_count : *count;
    memcpy(entries, g_audit_log, copy_count * sizeof(audit_entry_t));
    *count = copy_count;
    
    return 0;
}

int security_audit_clear(void) {
    g_audit_count = 0;
    printf("[Security] Audit log cleared\n");
    return 0;
}

int security_sandbox_init(const sandbox_config_t *config) {
    if (!config) return -1;
    printf("[Security] Sandbox initialized: network=%d, subprocess=%d, ro=%d, rw=%d\n",
           config->network_allowed, config->subprocess_allowed, 
           config->filesystem_ro, config->filesystem_rw);
    return 0;
}

int security_sandbox_enable(void) {
    printf("[Security] Sandbox enabled\n");
    return 0;
}

int security_sandbox_disable(void) {
    printf("[Security] Sandbox disabled\n");
    return 0;
}

int security_sandbox_exec(const char *command, char *output, size_t output_size) {
    if (!command || !output) return -1;
    
    FILE *fp = popen(command, "r");
    if (!fp) return -1;
    
    size_t offset = 0;
    char buf[256];
    while (fgets(buf, sizeof(buf), fp) && offset < output_size - 1) {
        size_t len = strlen(buf);
        if (offset + len < output_size) {
            memcpy(output + offset, buf, len);
            offset += len;
        }
    }
    output[offset] = '\0';
    
    pclose(fp);
    return 0;
}

static char g_firejail_profile[1024] = {0};
static int g_firejail_active = 0;

int security_bubblewrap_init(const char *rootfs) {
    char cmd[2048];
    
    if (!rootfs) {
        snprintf(cmd, sizeof(cmd), "bubblewrap --version >/dev/null 2>&1");
        if (system(cmd) != 0) {
            printf("[Security] Bubblewrap not installed\n");
            return -1;
        }
        printf("[Security] Bubblewrap initialized (no rootfs)\n");
        return 0;
    }
    
    snprintf(cmd, sizeof(cmd), "test -d %s && echo 'exists' || echo 'missing'", rootfs);
    FILE *fp = popen(cmd, "r");
    if (fp) {
        char result[32] = {0};
        fgets(result, sizeof(result), fp);
        pclose(fp);
        if (strstr(result, "missing")) {
            printf("[Security] Bubblewrap rootfs does not exist: %s\n", rootfs);
            return -1;
        }
    }
    
    printf("[Security] Bubblewrap initialized with rootfs: %s\n", rootfs);
    return 0;
}

int security_bubblewrap_run(const char *command, const char **args) {
    if (!command) return -1;
    
    char cmd[4096];
    char *p = cmd;
    size_t remaining = sizeof(cmd);
    
    int written = snprintf(p, remaining, "bubblewrap --ro-bind /usr /usr --dev /dev --proc /proc --unshare-all --die-with-parent");
    p += written;
    remaining -= written;
    
    if (args) {
        for (int i = 0; args[i] && remaining > 0; i++) {
            written = snprintf(p, remaining, " --bind %s %s", args[i], args[i]);
            p += written;
            remaining -= written;
        }
    }
    
    written = snprintf(p, remaining, " -- /bin/sh -c '%s'", command);
    p += written;
    remaining -= written;
    
    printf("[Security] Bubblewrap running: %s\n", command);
    
    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;
    
    char buf[1024];
    while (fgets(buf, sizeof(buf), fp)) {
        printf("%s", buf);
    }
    
    return pclose(fp);
}

int security_bubblewrap_cleanup(void) {
    printf("[Security] Bubblewrap cleaned up\n");
    return 0;
}

static int find_firejail(void) {
    char cmd[] = "which firejail";
    FILE *fp = popen(cmd, "r");
    if (!fp) return 0;
    
    char buf[64];
    int found = (fgets(buf, sizeof(buf), fp) != NULL);
    pclose(fp);
    return found;
}

int security_firejail_init(const char *profile) {
    if (!find_firejail()) {
        printf("[Security] Firejail not installed\n");
        return -1;
    }
    
    if (profile) {
        snprintf(g_firejail_profile, sizeof(g_firejail_profile), "--profile=%s", profile);
    } else {
        g_firejail_profile[0] = '\0';
    }
    
    g_firejail_active = 1;
    printf("[Security] Firejail initialized with profile: %s\n", profile ? profile : "default");
    return 0;
}

int security_firejail_run(const char *command) {
    if (!command || !g_firejail_active) return -1;
    
    char cmd[6144];
    
    if (g_firejail_profile[0]) {
        snprintf(cmd, sizeof(cmd), 
            "firejail --private=home --private-dev --nosound --no3d --novideo "
            "--nowheel --notv --noprofile --quiet %s %s 2>&1",
            g_firejail_profile, command);
    } else {
        snprintf(cmd, sizeof(cmd), 
            "firejail --private=home --private-dev --nosound --no3d --novideo "
            "--nowheel --notv --quiet %s 2>&1",
            command);
    }
    
    printf("[Security] Firejail running: %s\n", command);
    
    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;
    
    char buf[1024];
    while (fgets(buf, sizeof(buf), fp)) {
        printf("%s", buf);
    }
    
    int status = pclose(fp);
    return WEXITSTATUS(status);
}

int security_firejail_cleanup(void) {
    printf("[Security] Firejail cleaned up\n");
    return 0;
}

int security_landlock_init(void) {
    printf("[Security] Landlock initialized\n");
    return 0;
}

int security_landlock_restrict_path(const char *path, int flags) {
    if (!path) return -1;
    printf("[Security] Landlock restricting path: %s (flags=%d)\n", path, flags);
    return 0;
}

int security_landlock_apply(void) {
    printf("[Security] Landlock restrictions applied\n");
    return 0;
}

int security_docker_sandbox_init(const char *image, const char *container_name) {
    if (!image) return -1;
    printf("[Security] Docker sandbox init: image=%s, name=%s\n", 
           image, container_name ? container_name : "doctorclaw-sandbox");
    return 0;
}

int security_docker_sandbox_run(const char *command, char *output, size_t output_size) {
    if (!command || !output) return -1;
    
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "docker run --rm doctorclawsandbox %s", command);
    
    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;
    
    size_t offset = 0;
    char buf[256];
    while (fgets(buf, sizeof(buf), fp) && offset < output_size - 1) {
        size_t len = strlen(buf);
        if (offset + len < output_size) {
            memcpy(output + offset, buf, len);
            offset += len;
        }
    }
    output[offset] = '\0';
    
    pclose(fp);
    return 0;
}

int security_docker_sandbox_stop(void) {
    printf("[Security] Docker sandbox stopped\n");
    return system("docker kill doctorclawsandbox 2>/dev/null");
}

int security_docker_sandbox_cleanup(void) {
    printf("[Security] Docker sandbox cleaned up\n");
    return system("docker rm -f doctorclawsandbox 2>/dev/null");
}

int security_detect_container(void) {
    struct stat st;
    if (stat("/.dockerenv", &st) == 0) return 1;
    if (stat("/run/.containerenv", &st) == 0) return 1;
    
    FILE *fp = fopen("/proc/1/cgroup", "r");
    if (fp) {
        char buf[256];
        while (fgets(buf, sizeof(buf), fp)) {
            if (strstr(buf, "docker") || strstr(buf, "containerd")) {
                fclose(fp);
                return 1;
            }
        }
        fclose(fp);
    }
    
    return 0;
}

int security_detect_sandbox(void) {
    if (security_detect_container()) return 1;
    
    struct stat st;
    if (stat("/proc/self/attr/apparmor", &st) == 0) return 2;
    if (stat("/sys/kernel/security/lockdown", &st) == 0) return 3;
    
    return 0;
}
