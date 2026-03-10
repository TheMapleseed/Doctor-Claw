/**
 * Intrusion Detection System — signature and anomaly checks.
 */

#include "ids.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static ids_config_t g_config;
static int g_initialized = 0;
static uint32_t g_body_baseline = 4096;

static const struct {
    const char *name;
    const char *pattern;
} g_builtin[] = {
    { "sql_comment",   "--" },
    { "sql_union",     "union " },
    { "sql_select",    "select " },
    { "sql_drop",      "drop table" },
    { "sql_sleep",     "sleep(" },
    { "shell_cmd",     "; /bin/" },
    { "shell_sh",      "| sh " },
    { "shell_bash",    "$(bash" },
    { "xss_script",    "<script" },
    { "xss_onerror",   "onerror=" },
    { "path_etc",      "/etc/passwd" },
    { "path_win",      "..\\" },
    { "ldap_inject",   "*)(uid=" },
    { "null_byte",     "%00" },
    { "crlf",          "\r\n" },
    { NULL, NULL }
};

static bool match_pattern(const char *haystack, size_t len, const char *pattern, bool case_insensitive) {
    if (!haystack || !pattern) return false;
    size_t plen = strlen(pattern);
    if (plen == 0 || plen > len) return false;
    for (size_t i = 0; i <= len - plen; i++) {
        bool match = true;
        for (size_t j = 0; j < plen; j++) {
            char a = haystack[i + j];
            char b = pattern[j];
            if (case_insensitive) {
                a = (char)tolower((unsigned char)a);
                b = (char)tolower((unsigned char)b);
            }
            if (a != b) { match = false; break; }
        }
        if (match) return true;
    }
    return false;
}

void ids_config_defaults(ids_config_t *cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->max_body_bytes = 0;
    cfg->anomaly_body_size_enabled = true;
    cfg->body_size_baseline = 8192;
}

int ids_init(const ids_config_t *cfg) {
    if (g_initialized) return 0;
    if (cfg) {
        g_config = *cfg;
    } else {
        ids_config_defaults(&g_config);
    }
    g_config.signature_count = 0;
    for (size_t i = 0; g_builtin[i].name != NULL && g_config.signature_count < IDS_SIGNATURE_MAX; i++) {
        snprintf(g_config.signatures[g_config.signature_count].name, sizeof(g_config.signatures[0].name), "%s", g_builtin[i].name);
        snprintf(g_config.signatures[g_config.signature_count].pattern, sizeof(g_config.signatures[0].pattern), "%s", g_builtin[i].pattern);
        g_config.signatures[g_config.signature_count].case_insensitive = true;
        g_config.signature_count++;
    }
    g_body_baseline = (uint32_t)g_config.body_size_baseline;
    g_initialized = 1;
    return 0;
}

void ids_shutdown(void) {
    g_initialized = 0;
}

ids_result_t ids_check(
    const char *method,
    const char *path,
    const char *body,
    size_t body_len,
    char *reason_str,
    size_t reason_size
) {
    (void)method;
    if (!g_initialized) return IDS_OK;
    if (reason_str && reason_size > 0) reason_str[0] = '\0';

    /* Signature check on path and body */
    if (path) {
        size_t path_len = strlen(path);
        for (size_t i = 0; i < g_config.signature_count; i++) {
            if (match_pattern(path, path_len, g_config.signatures[i].pattern, g_config.signatures[i].case_insensitive)) {
                if (reason_str && reason_size > 0)
                    snprintf(reason_str, reason_size, "signature:%s in path", g_config.signatures[i].name);
                return IDS_ALERT_SIGNATURE;
            }
        }
    }
    if (body && body_len > 0) {
        size_t scan_len = body_len > 8192 ? 8192 : body_len;
        for (size_t i = 0; i < g_config.signature_count; i++) {
            if (match_pattern(body, scan_len, g_config.signatures[i].pattern, g_config.signatures[i].case_insensitive)) {
                if (reason_str && reason_size > 0)
                    snprintf(reason_str, reason_size, "signature:%s in body", g_config.signatures[i].name);
                return IDS_ALERT_SIGNATURE;
            }
        }
    }

    /* Anomaly: body size far above baseline */
    if (g_config.anomaly_body_size_enabled && body_len > 0) {
        uint32_t threshold = g_body_baseline > 0 ? g_body_baseline * 4 : 65536;
        if (body_len > threshold) {
            if (reason_str && reason_size > 0)
                snprintf(reason_str, reason_size, "anomaly:body_size %zu > %u", body_len, threshold);
            return IDS_ALERT_ANOMALY;
        }
    }

    if (g_config.max_body_bytes > 0 && body_len > g_config.max_body_bytes) {
        if (reason_str && reason_size > 0)
            snprintf(reason_str, reason_size, "body_size %zu exceeds max %zu", body_len, (size_t)g_config.max_body_bytes);
        return IDS_ALERT_ANOMALY;
    }

    return IDS_OK;
}

int ids_add_signature(const char *name, const char *pattern, bool case_insensitive) {
    if (!g_initialized || !name || !pattern) return -1;
    if (g_config.signature_count >= IDS_SIGNATURE_MAX) return -1;
    snprintf(g_config.signatures[g_config.signature_count].name, sizeof(g_config.signatures[0].name), "%s", name);
    snprintf(g_config.signatures[g_config.signature_count].pattern, sizeof(g_config.signatures[0].pattern), "%s", pattern);
    g_config.signatures[g_config.signature_count].case_insensitive = case_insensitive;
    g_config.signature_count++;
    return 0;
}

void ids_update_body_baseline(size_t body_len) {
    if (body_len == 0) return;
    uint32_t v = (uint32_t)body_len;
    g_body_baseline = (g_body_baseline + v) / 2;
}
