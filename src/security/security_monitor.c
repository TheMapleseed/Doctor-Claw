/**
 * Security monitoring for the gateway — injection prevention, rate limiting, audit.
 * Inspired by OpenFang (https://github.com/RightNow-AI/openfang) security layers.
 */

#include "security_monitor.h"
#include "security.h"
#include "observability.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <strings.h>
#include <pthread.h>

#define RATE_BUCKETS 256
#define RATE_KEY_MAX 64

typedef struct {
    char ip[SECMON_IP_MAX];
    uint32_t count;
    time_t window_end;
} rate_bucket_t;

static security_monitor_config_t g_config;
static rate_bucket_t g_rate_buckets[RATE_BUCKETS];
static pthread_mutex_t g_rate_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint64_t g_rate_limit_hits = 0;
static uint64_t g_injection_rejects = 0;
static int g_initialized = 0;

/* Patterns that suggest prompt override / instruction injection (case-insensitive scan). */
static const char *const g_injection_patterns[] = {
    "ignore previous",
    "ignore all previous",
    "disregard previous",
    "forget everything",
    "you are now",
    "new instructions",
    "system:",
    "system prompt",
    "override instructions",
    "ignore above",
    "disregard above",
    "new identity",
    "pretend you are",
    "act as if",
    "from now on you",
    "your new role",
    "jailbreak",
    "developer mode",
    "DAN mode",
    "do anything now",
    "bypass",
    "ignore safety",
    "reveal your prompt",
    "repeat the above",
    "output your instructions",
    "what is your system prompt",
    "show your prompt",
    NULL
};

static unsigned hash_ip(const char *ip) {
    unsigned h = 0;
    if (!ip) return 0;
    for (; *ip; ip++) {
        h = 31 * h + (unsigned char)*ip;
    }
    return h % RATE_BUCKETS;
}

static bool contains_injection_pattern(const char *body, size_t len) {
    if (!body || len == 0) return false;
    /* Use a small sliding window for the first 16K to avoid scanning huge bodies. */
    size_t scan_len = len > 16384 ? 16384 : len;
    char *lower = (char *)malloc(scan_len + 1);
    if (!lower) return false;
    for (size_t i = 0; i < scan_len; i++) {
        lower[i] = (char)tolower((unsigned char)body[i]);
    }
    lower[scan_len] = '\0';

    for (size_t p = 0; g_injection_patterns[p] != NULL; p++) {
        if (strstr(lower, g_injection_patterns[p]) != NULL) {
            free(lower);
            return true;
        }
    }
    free(lower);
    return false;
}

static bool path_has_traversal(const char *path) {
    if (!path) return true;
    if (strstr(path, "..") != NULL) return true;
    if (strstr(path, "%2e%2e") != NULL) return true;
    if (strstr(path, "%252e%252e") != NULL) return true;
    if (strstr(path, "..%2f") != NULL) return true;
    if (strstr(path, "%2f..") != NULL) return true;
    return false;
}

static bool method_allowed(const char *method) {
    if (!method || !method[0]) return false;
    switch (method[0]) {
        case 'G': case 'g':
            return strcmp(method, "GET") == 0 || strcasecmp(method, "get") == 0;
        case 'P': case 'p':
            return strcmp(method, "POST") == 0 || strcasecmp(method, "post") == 0 ||
                   strcmp(method, "PUT") == 0 || strcasecmp(method, "put") == 0 ||
                   strcmp(method, "PATCH") == 0 || strcasecmp(method, "patch") == 0;
        case 'H': case 'h':
            return strcmp(method, "HEAD") == 0 || strcasecmp(method, "head") == 0;
        case 'O': case 'o':
            return strcmp(method, "OPTIONS") == 0 || strcasecmp(method, "options") == 0;
        case 'D': case 'd':
            return strcmp(method, "DELETE") == 0 || strcasecmp(method, "delete") == 0;
        default:
            return false;
    }
}

void security_monitor_config_defaults(security_monitor_config_t *cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->rate_limit_enabled = true;
    cfg->rate_limit_per_window = SECMON_RATE_LIMIT_DEFAULT;
    cfg->rate_window_sec = SECMON_RATE_WINDOW_SEC;
    cfg->injection_scan_enabled = true;
    cfg->path_traversal_check = true;
    cfg->max_body_size = 0;
    cfg->audit_rejected = true;
}

int security_monitor_init(const security_monitor_config_t *cfg) {
    if (g_initialized) return 0;
    if (cfg) {
        g_config = *cfg;
    } else {
        security_monitor_config_defaults(&g_config);
    }
    memset(g_rate_buckets, 0, sizeof(g_rate_buckets));
    g_rate_limit_hits = 0;
    g_injection_rejects = 0;
    g_initialized = 1;
    log_info("Security monitor initialized (rate_limit=%d, injection_scan=%d, path_check=%d)",
             g_config.rate_limit_enabled, g_config.injection_scan_enabled, g_config.path_traversal_check);
    return 0;
}

void security_monitor_shutdown(void) {
    g_initialized = 0;
}

security_monitor_result_t security_monitor_check_request(
    const char *client_ip,
    const char *method,
    const char *path,
    const char *body,
    size_t body_len,
    char *reason_str,
    size_t reason_size
) {
    if (!client_ip) client_ip = "unknown";

    if (reason_str && reason_size > 0) reason_str[0] = '\0';

    if (!method_allowed(method)) {
        if (reason_str && reason_size > 0)
            snprintf(reason_str, reason_size, "method not allowed: %s", method);
        return SECMON_METHOD_INVALID;
    }

    if (g_config.path_traversal_check && path_has_traversal(path)) {
        if (reason_str && reason_size > 0)
            snprintf(reason_str, reason_size, "path traversal detected");
        return SECMON_PATH_TRAVERSAL;
    }

    if (g_config.max_body_size > 0 && body_len > g_config.max_body_size) {
        if (reason_str && reason_size > 0)
            snprintf(reason_str, reason_size, "body too large: %zu", body_len);
        return SECMON_BODY_TOO_LARGE;
    }

    if (g_config.rate_limit_enabled) {
        pthread_mutex_lock(&g_rate_mutex);
        unsigned idx = hash_ip(client_ip);
        time_t now = time(NULL);
        if (g_rate_buckets[idx].window_end <= now ||
            strncmp(g_rate_buckets[idx].ip, client_ip, SECMON_IP_MAX - 1) != 0) {
            snprintf(g_rate_buckets[idx].ip, sizeof(g_rate_buckets[idx].ip), "%s", client_ip);
            g_rate_buckets[idx].count = 0;
            g_rate_buckets[idx].window_end = now + (time_t)g_config.rate_window_sec;
        }
        g_rate_buckets[idx].count++;
        uint32_t count = g_rate_buckets[idx].count;
        pthread_mutex_unlock(&g_rate_mutex);

        if (count > g_config.rate_limit_per_window) {
            g_rate_limit_hits++;
            observability_record_global("security_monitor_rate_limited_total", 1.0);
            if (reason_str && reason_size > 0)
                snprintf(reason_str, reason_size, "rate limit exceeded for %s", client_ip);
            return SECMON_RATE_LIMITED;
        }
    }

    if (g_config.injection_scan_enabled && body && body_len > 0) {
        if (contains_injection_pattern(body, body_len)) {
            g_injection_rejects++;
            observability_record_global("security_monitor_injection_rejected_total", 1.0);
            if (reason_str && reason_size > 0)
                snprintf(reason_str, reason_size, "injection pattern detected in body");
            return SECMON_INJECTION_SUSPECTED;
        }
    }

    return SECMON_OK;
}

void security_monitor_record_request(const char *client_ip) {
    (void)client_ip;
    /* Rate limit state already updated in check_request when we allow. Nothing extra. */
}

void security_monitor_audit_reject(
    security_monitor_result_t result,
    const char *client_ip,
    const char *method,
    const char *path,
    const char *reason
) {
    char action[128];
    char target[256];
    snprintf(action, sizeof(action), "security_monitor_reject:%d", (int)result);
    snprintf(target, sizeof(target), "ip=%s method=%s path=%s reason=%s",
             client_ip ? client_ip : "unknown",
             method ? method : "",
             path ? path : "",
             reason ? reason : "");
    security_audit_log(action, target, false);
    log_info("[Security] Rejected request: %s", target);
}

/* Private IP ranges and cloud metadata hosts — SSRF mitigation. */
static bool host_is_private_or_metadata(const char *host) {
    if (!host || !host[0]) return true;
    if (strcmp(host, "localhost") == 0) return true;
    if (strncmp(host, "127.", 4) == 0) return true;
    if (strncmp(host, "10.", 3) == 0) return true;
    if (strncmp(host, "192.168.", 8) == 0) return true;
    if (strncmp(host, "172.16.", 7) == 0 || strncmp(host, "172.17.", 7) == 0 ||
        strncmp(host, "172.18.", 7) == 0 || strncmp(host, "172.19.", 7) == 0 ||
        (strncmp(host, "172.", 4) == 0 && host[4] >= '1' && host[4] <= '3' && host[5] == '.')) return true;
    if (strcmp(host, "169.254.169.254") == 0) return true;  /* cloud metadata */
    if (strcasecmp(host, "metadata.google.internal") == 0) return true;
    if (strstr(host, "metadata") != NULL && strstr(host, "google") != NULL) return true;
    if (strcasecmp(host, "instance-data") == 0) return true;
    return false;
}

bool security_monitor_url_safe_for_fetch(const char *url) {
    if (!url || !url[0]) return false;
    /* Require http or https. */
    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0)
        return false;
    const char *authority = strstr(url, "://");
    if (!authority) return false;
    authority += 3;
    const char *path_start = strchr(authority, '/');
    char host[256];
    size_t host_len = path_start ? (size_t)(path_start - authority) : strlen(authority);
    if (host_len >= sizeof(host)) return false;
    memcpy(host, authority, host_len);
    host[host_len] = '\0';
    /* Strip port if present. */
    char *colon = strchr(host, ':');
    if (colon) *colon = '\0';
    return !host_is_private_or_metadata(host);
}

uint64_t security_monitor_get_rate_limit_count(void) {
    return g_rate_limit_hits;
}

uint64_t security_monitor_get_injection_reject_count(void) {
    return g_injection_rejects;
}
