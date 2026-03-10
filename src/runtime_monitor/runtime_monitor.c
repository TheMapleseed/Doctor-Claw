/**
 * Runtime monitoring: security/penetration events notify the user and keep the agent aware.
 */

#include "runtime_monitor.h"
#include "channels.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#define CONNECTION_WINDOW_SEC  60
#define CONNECTION_PROBE_THRESHOLD 25
#define CONNECTION_IPS_MAX    64

typedef struct {
    char ip[RUNTIME_MONITOR_IP_MAX];
    uint64_t last_seen_sec;
    int count;
} connection_bucket_t;

static runtime_monitor_event_t g_events[RUNTIME_MONITOR_EVENT_MAX];
static size_t g_event_head = 0;
static size_t g_event_count = 0;
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool g_notify_user = true;
static bool g_agent_aware = true;
static int g_initialized = 0;

static connection_bucket_t g_conn_buckets[CONNECTION_IPS_MAX];
static size_t g_conn_bucket_count = 0;

static const char *event_type_str(runtime_event_type_t t) {
    switch (t) {
        case RUNTIME_EVENT_SECURITY_REJECT: return "Security reject";
        case RUNTIME_EVENT_RATE_LIMIT:     return "Rate limit";
        case RUNTIME_EVENT_INJECTION:      return "Injection attempt";
        case RUNTIME_EVENT_PATH_TRAVERSAL: return "Path traversal";
        case RUNTIME_EVENT_PORT_PROBE:     return "Port probe";
        case RUNTIME_EVENT_ERROR:          return "Error";
        default:                           return "Event";
    }
}

static void push_event(runtime_event_type_t type, const char *source, const char *ip, const char *details) {
    uint64_t now = (uint64_t)time(NULL);
    pthread_mutex_lock(&g_mutex);
    size_t idx = (g_event_head + g_event_count) % RUNTIME_MONITOR_EVENT_MAX;
    if (g_event_count >= RUNTIME_MONITOR_EVENT_MAX) {
        g_event_head = (g_event_head + 1) % RUNTIME_MONITOR_EVENT_MAX;
        g_event_count--;
    }
    g_events[idx].type = type;
    snprintf(g_events[idx].source, sizeof(g_events[idx].source), "%s", source ? source : "");
    snprintf(g_events[idx].ip, sizeof(g_events[idx].ip), "%s", ip ? ip : "");
    snprintf(g_events[idx].details, sizeof(g_events[idx].details), "%s", details ? details : "");
    g_events[idx].timestamp_sec = now;
    g_event_count++;
    pthread_mutex_unlock(&g_mutex);

    log_info("[RuntimeMonitor] %s from %s: %s", event_type_str(type), ip ? ip : "-", details ? details : "");
}

int runtime_monitor_init(void) {
    if (g_initialized) return 0;
    memset(g_events, 0, sizeof(g_events));
    memset(g_conn_buckets, 0, sizeof(g_conn_buckets));
    g_event_head = 0;
    g_event_count = 0;
    g_conn_bucket_count = 0;
    g_notify_user = true;
    g_agent_aware = true;
    g_initialized = 1;
    log_info("Runtime monitor initialized (notify_user=1, agent_aware=1)");
    return 0;
}

void runtime_monitor_shutdown(void) {
    g_initialized = 0;
}

void runtime_monitor_set_notify_user(bool enable) {
    g_notify_user = enable;
}

void runtime_monitor_set_agent_aware(bool enable) {
    g_agent_aware = enable;
}

void runtime_monitor_event(
    runtime_event_type_t type,
    const char *source,
    const char *ip,
    const char *details
) {
    if (!g_initialized) return;
    push_event(type, source, ip, details);

    if (g_notify_user) {
        char alert[512];
        snprintf(alert, sizeof(alert), "⚠️ Doctor Claw: %s\nFrom: %s\n%s",
                 event_type_str(type), ip && ip[0] ? ip : "unknown", details && details[0] ? details : "");
        if (channels_send_alert(alert) == 0) {
            log_info("Alert sent to user channels");
        }
    }
}

void runtime_monitor_connection(const char *ip) {
    if (!g_initialized || !ip || !ip[0]) return;
    uint64_t now = (uint64_t)time(NULL);
    uint64_t cutoff = now - CONNECTION_WINDOW_SEC;

    pthread_mutex_lock(&g_mutex);
    size_t i;
    for (i = 0; i < g_conn_bucket_count; i++) {
        if (strcmp(g_conn_buckets[i].ip, ip) == 0) {
            if (g_conn_buckets[i].last_seen_sec < cutoff) {
                g_conn_buckets[i].count = 0;
            }
            g_conn_buckets[i].last_seen_sec = now;
            g_conn_buckets[i].count++;
            if (g_conn_buckets[i].count >= CONNECTION_PROBE_THRESHOLD) {
                pthread_mutex_unlock(&g_mutex);
                runtime_monitor_event(RUNTIME_EVENT_PORT_PROBE, "gateway", ip,
                    "Many connections in short time (possible port scan or probe)");
                pthread_mutex_lock(&g_mutex);
                g_conn_buckets[i].count = 0;
            }
            pthread_mutex_unlock(&g_mutex);
            return;
        }
    }
    if (g_conn_bucket_count < CONNECTION_IPS_MAX) {
        snprintf(g_conn_buckets[g_conn_bucket_count].ip, sizeof(g_conn_buckets[0].ip), "%s", ip);
        g_conn_buckets[g_conn_bucket_count].last_seen_sec = now;
        g_conn_buckets[g_conn_bucket_count].count = 1;
        g_conn_bucket_count++;
    } else {
        /* Evict oldest bucket */
        size_t oldest = 0;
        for (size_t j = 1; j < g_conn_bucket_count; j++) {
            if (g_conn_buckets[j].last_seen_sec < g_conn_buckets[oldest].last_seen_sec)
                oldest = j;
        }
        snprintf(g_conn_buckets[oldest].ip, sizeof(g_conn_buckets[0].ip), "%s", ip);
        g_conn_buckets[oldest].last_seen_sec = now;
        g_conn_buckets[oldest].count = 1;
    }
    pthread_mutex_unlock(&g_mutex);
}

int runtime_monitor_get_recent_for_agent(char *buf, size_t buf_size) {
    if (!buf || buf_size < 128 || !g_agent_aware || !g_initialized) return -1;
    pthread_mutex_lock(&g_mutex);
    if (g_event_count == 0) {
        pthread_mutex_unlock(&g_mutex);
        return -1;
    }
    size_t n = g_event_count > 10 ? 10 : g_event_count;
    size_t offset = 0;
    offset += (size_t)snprintf(buf + offset, buf_size - offset,
        "System runtime alerts (recent): ");
    for (size_t i = 0; i < n && offset < buf_size - 80; i++) {
        size_t idx = (g_event_head + g_event_count - 1 - i + RUNTIME_MONITOR_EVENT_MAX) % RUNTIME_MONITOR_EVENT_MAX;
        runtime_monitor_event_t *e = &g_events[idx];
        struct tm *tm = localtime((time_t *)&e->timestamp_sec);
        char tbuf[32];
        if (tm) strftime(tbuf, sizeof(tbuf), "%H:%M", tm);
        else tbuf[0] = '\0';
        offset += (size_t)snprintf(buf + offset, buf_size - offset,
            "[%s] %s from %s; ", tbuf, event_type_str(e->type), e->ip[0] ? e->ip : "?");
    }
    pthread_mutex_unlock(&g_mutex);
    buf[offset] = '\0';
    return 0;
}

int runtime_monitor_get_recent_events(runtime_monitor_event_t *out, size_t max_count, size_t *count) {
    if (!out || !count) return -1;
    pthread_mutex_lock(&g_mutex);
    size_t n = g_event_count < max_count ? g_event_count : max_count;
    for (size_t i = 0; i < n; i++) {
        size_t idx = (g_event_head + g_event_count - 1 - i + RUNTIME_MONITOR_EVENT_MAX) % RUNTIME_MONITOR_EVENT_MAX;
        out[i] = g_events[idx];
    }
    *count = n;
    pthread_mutex_unlock(&g_mutex);
    return 0;
}
