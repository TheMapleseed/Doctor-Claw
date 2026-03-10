#ifndef DOCTORCLAW_IDS_H
#define DOCTORCLAW_IDS_H

/**
 * Intrusion Detection System — signature and anomaly checks.
 * OpenFang-inspired; complements security_monitor with pattern-based detection.
 */

#include "c23_check.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define IDS_SIGNATURE_MAX  64
#define IDS_PATTERN_MAX    128
#define IDS_REASON_MAX    128

typedef enum {
    IDS_OK = 0,
    IDS_ALERT_SIGNATURE,   /* matched attack signature */
    IDS_ALERT_ANOMALY     /* anomaly (e.g. body size spike) */
} ids_result_t;

typedef struct {
    char name[64];
    char pattern[IDS_PATTERN_MAX];
    bool case_insensitive;
} ids_signature_t;

typedef struct {
    ids_signature_t signatures[IDS_SIGNATURE_MAX];
    size_t signature_count;
    size_t max_body_bytes;        /* 0 = no body size check */
    bool anomaly_body_size_enabled;
    uint32_t body_size_baseline;   /* typical max; exceed by factor → anomaly */
} ids_config_t;

/** Set defaults (built-in signatures for common attacks). */
void ids_config_defaults(ids_config_t *cfg);

/** Initialize IDS with config. Call once at startup. */
int ids_init(const ids_config_t *cfg);

/** Shutdown. */
void ids_shutdown(void);

/**
 * Check request (path, headers, body). Returns IDS_OK or alert type.
 * reason_str filled when alert (e.g. "signature: sql_injection").
 */
ids_result_t ids_check(
    const char *method,
    const char *path,
    const char *body,
    size_t body_len,
    char *reason_str,
    size_t reason_size
);

/** Add a custom signature (e.g. from config). Returns 0 on success. */
int ids_add_signature(const char *name, const char *pattern, bool case_insensitive);

/** Update baseline for anomaly (e.g. rolling avg of body size). Optional. */
void ids_update_body_baseline(size_t body_len);

#endif
