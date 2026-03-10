#ifndef DOCTORCLAW_SECURITY_MONITOR_H
#define DOCTORCLAW_SECURITY_MONITOR_H

#include "c23_check.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Security monitoring for the gateway — inspired by OpenFang's defense-in-depth.
 * Aims to prevent malicious packet/input injection and mitigate abuse against the owner.
 *
 * - Rate limiting: per-IP to prevent DoS and resource exhaustion
 * - Injection detection: prompt-override and data-exfiltration patterns in request body
 * - Path traversal: reject paths with ".." or encoded variants
 * - Audit: log rejected requests for forensics
 * - Optional SSRF checks for URLs used in outbound requests
 */

#define SECMON_REASON_MAX 128
#define SECMON_IP_MAX 64
#define SECMON_RATE_LIMIT_DEFAULT 120   /* requests per window */
#define SECMON_RATE_WINDOW_SEC 60

typedef enum {
    SECMON_OK = 0,
    SECMON_RATE_LIMITED,
    SECMON_INJECTION_SUSPECTED,
    SECMON_PATH_TRAVERSAL,
    SECMON_BODY_TOO_LARGE,
    SECMON_METHOD_INVALID,
    SECMON_REJECT_OTHER
} security_monitor_result_t;

typedef struct {
    bool rate_limit_enabled;
    uint32_t rate_limit_per_window;  /* max requests per IP per window */
    uint32_t rate_window_sec;
    bool injection_scan_enabled;
    bool path_traversal_check;
    size_t max_body_size;            /* 0 = use caller's limit */
    bool audit_rejected;
} security_monitor_config_t;

/** Initialize config to defaults (rate limit on, injection scan on, path check on, audit on). */
void security_monitor_config_defaults(security_monitor_config_t *cfg);

/** Initialize the security monitor (rate-limit state, etc.). Call once at startup. */
int security_monitor_init(const security_monitor_config_t *cfg);

/** Shutdown and free state. */
void security_monitor_shutdown(void);

/**
 * Check an incoming HTTP request. Call after parsing, before handle_request.
 * Returns SECMON_OK if the request is allowed, else a rejection reason.
 * If rejected, reason_str (if non-NULL) is filled with a short message for logging.
 */
security_monitor_result_t security_monitor_check_request(
    const char *client_ip,
    const char *method,
    const char *path,
    const char *body,
    size_t body_len,
    char *reason_str,
    size_t reason_size
);

/** Record that a request was allowed (for rate limiting). Call only when SECMON_OK. */
void security_monitor_record_request(const char *client_ip);

/** Audit a rejected request (log + optional integration with security_audit_log). */
void security_monitor_audit_reject(
    security_monitor_result_t result,
    const char *client_ip,
    const char *method,
    const char *path,
    const char *reason
);

/**
 * SSRF mitigation: validate URL for outbound use.
 * Returns true if URL is considered safe (no private IP, no cloud metadata host).
 * Use before fetching URLs derived from user input or config.
 */
bool security_monitor_url_safe_for_fetch(const char *url);

/** Get current count of rate-limited requests (for metrics). */
uint64_t security_monitor_get_rate_limit_count(void);

/** Get current count of injection-suspected rejections (for metrics). */
uint64_t security_monitor_get_injection_reject_count(void);

#endif
